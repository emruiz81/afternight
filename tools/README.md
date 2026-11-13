# AfterNight — Tools

Offline utilities for building catalog data that AfterNight downloads at runtime.

---

## Gaia DR3 Catalog Generator

`generate_gaia_catalog.py` queries the [ESA Gaia DR3 Archive](https://gea.esac.esa.int/archive/) via TAP and produces chunked `.ansp` catalogs consumed by the engine's Gaia local catalog reader.

### Prerequisites

- Python 3.10+
- No external packages required (stdlib only: `urllib`, `csv`, `struct`)

### Catalog Packs

| Preset | Mag limit | Approx. stars | Size |
|--------|-----------|---------------|------|
| `g12`  | G ≤ 12    | ~6.5 M        | ~397 MB |
| `g14`  | G ≤ 14    | ~65 M         | ~3.9 GB |
| `g16`  | G ≤ 16    | ~430 M        | ~26 GB |

### Generating a catalog

```bash
# Basic usage with a preset
python3 tools/generate_gaia_catalog.py --tap-official --preset g14 catalogs/gaia_dr3_g14

# All three packs
python3 tools/generate_gaia_catalog.py --tap-official --preset g12 catalogs/gaia_dr3_g12
python3 tools/generate_gaia_catalog.py --tap-official --preset g14 catalogs/gaia_dr3_g14
python3 tools/generate_gaia_catalog.py --tap-official --preset g16 catalogs/gaia_dr3_g16
```

Each run queries the ESA TAP service and writes the output directory:

```
catalogs/gaia_dr3_g14/
  catalog_meta.json
  gaia_dr3_g14_chunk_00.ansp
  gaia_dr3_g14_chunk_01.ansp
  ...
  gaia_dr3_g14_chunk_47.ansp
```

The generator now commits each completed TAP pixel to a durable per-pixel store first, rebuilds temporary per-tile files from that store, and then consolidates those tiles into the final chunk files. A successful run removes the temporary TAP store, chunk checkpoint, and tile directory automatically.

**Timing**: `g12` takes roughly 30–60 minutes, `g14` several hours, `g16` can take a day or more depending on ESA server load.

### Performance tuning

By default, the generator runs **4 parallel worker threads** for TAP pixel queries and pass-2 chunk writes. Increase `--workers` on fast network connections:

```bash
# More aggressive TAP parallelism
python3 tools/generate_gaia_catalog.py --tap-official --preset g14 \
    --workers 8 \
    catalogs/gaia_dr3_g14
```

The `--max-open-files` limit is auto-detected from the OS file-descriptor soft limit at runtime (capped to the exact tile count, which is 12 288 for HEALPix order 5). This avoids the file-handle LRU thrashing that would occur with the old hardcoded default of 128. Override with `--max-open-files N` if you need a lower cap.

### Output format

Each `.ansp` file contains:

- a 16-byte fixed preamble with magic/version metadata
- a UTF-8 JSON chunk header
- a packed tile index table
- contiguous 64-byte Gaia v3 star records

The v3 record extends the older Gaia row with astrophysical parameters for SPCC:

- `teff_gspphot`
- `logg_gspphot`
- `mh_gspphot`
- `ag_gspphot`
- `ebpminrp_gspphot`

The root `catalog_meta.json` advertises `format_version: 3`, `storage: "chunks"`, the chunk list, per-chunk sizes, and a SHA-256 for each full `.ansp` file.

### Partial / resumed builds

Use `--tap-include-pixel` to restrict work to specific HEALPix pixels at the initial partitioning level (default level 3, 768 pixels). Useful for testing or resuming a failed build:

```bash
# Build only pixel 0 at level 3 (for testing)
python3 tools/generate_gaia_catalog.py --tap-official --preset g14 \
    --tap-include-pixel 0 catalogs/gaia_dr3_g14_test
```

### Failure recovery

`--resume` is safe across interruptions for TAP-backed runs. Completed HEALPix pixels are preserved in:

```
<output_dir>/.tmp_tap_pixels_order<N>
```

and partially completed chunk manifests are checkpointed in:

```
<output_dir>/.tmp_chunk_manifests.json
```

The temporary tile directory is still used, but it is rebuilt from the committed TAP pixel files on resume instead of being trusted as the source of truth:

```
<output_dir>/.tmp_healpix_order<N>
```

If the ESA TAP endpoint starts returning repeated transient DNS / 5xx / connection-reset failures, the generator aborts after 300 seconds of uninterrupted TAP request failures by default instead of spinning forever. Tune that with `--tap-max-outage-seconds`, or disable it with `--tap-max-outage-seconds 0`.

For resumed runs after service instability, reduce concurrency first:

```bash
python3 tools/generate_gaia_catalog.py --tap-official --preset g14 --resume \
    --workers 1 \
    catalogs/gaia_dr3_g14
```

For example, if you intentionally want to discard all resumable state and start over:

```bash
rm -rf catalogs/gaia_dr3_g14/.tmp_tap_pixels_order5
rm -rf catalogs/gaia_dr3_g14/.tmp_healpix_order5
rm -f  catalogs/gaia_dr3_g14/.tmp_chunk_manifests.json
```

### Custom catalogs

All preset fields can be overridden:

```bash
python3 tools/generate_gaia_catalog.py --tap-official \
    --catalog-id gaia_dr3_custom \
    --catalog-name "Gaia DR3 Custom" \
    --mag-limit 13.5 \
    --healpix-order 5 \
    catalogs/gaia_dr3_custom
```

### Local CSV input mode

If you have Gaia DR3 bulk CSV/CSV.GZ files downloaded from the ESA archive, you can generate catalogs from local files instead of TAP:

```bash
# Single file
python3 tools/generate_gaia_catalog.py GaiaSource_000000.csv.gz catalogs/gaia_dr3_g14 --preset g14

# Directory of files
python3 tools/generate_gaia_catalog.py /data/gaia_dr3/ catalogs/gaia_dr3_g14 --preset g14

# Multiple inputs
python3 tools/generate_gaia_catalog.py GaiaSource_000000.csv.gz catalogs/gaia_dr3_g14 \
    --extra-input GaiaSource_000001.csv.gz \
    --input-dir /data/more_gaia/ \
    --preset g14
```

---

## Other Tools

| File | Description |
|------|-------------|
| `generate_bright_star_catalog.py` | Generates the bundled bright-star catalog (HYG v4.2 to visual magnitude 9) the star labels, Annotate Image and the catalog search read; `--archive` reads a downloaded copy of the pinned archive |
| `generate_deep_sky_catalog.py` | Generates the bundled deep-sky catalog (NGC 2000.0 with the Messier, Caldwell and common names and the curated extents) the overlays, Annotate Image and the catalog search read. `test_generate_deep_sky_catalog.py` holds the generator and the catalog the product ships to each other: `python -m unittest tools.test_generate_deep_sky_catalog` from the repository's root, or `python tools/test_generate_deep_sky_catalog.py` |
| `generate_spcc_catalog.py` | Generates deterministic miniature SPCC `.anxp` catalogs and local JSONL-backed SPCC catalog packs, including regional-cache request sidecars |
| `export_spcc_gaia_xp_metadata.py` | Exports resumable Gaia DR3 `gaia_source` + `xp_summary` metadata CSV.GZ parts from official TAP or local CSV inputs |
| `download_spcc_gaia_xp_sampled_files.py` | Downloads official Gaia DR3 XP sampled spectrum CSV.GZ files into a resumable local staging directory and manifest |
| `export_spcc_gaia_full_xp_sampled_sources.py` | Joins Gaia DR3 source/xp_summary metadata with XP sampled spectra into resumable full-sky SPCC JSONL source shards |
| `build_spcc_full_xp_sampled_catalog.py` | Builds resumable, capped full-sky SPCC `.anxp` catalogs from prejoined Gaia DR3 source metadata plus XP sampled spectra JSONL exports |
| `approve_spcc_catalog_rights.py` | Records reviewed Gaia DR3 redistribution, citation, and mirror-hosting approval in an SPCC `catalog_meta.json` |
| `validate_spcc_catalog.py` | Validates SPCC catalog metadata, checksums, records, coverage summaries, request sidecars, and source-id indexes |
| `check_spcc_support_envelope.py` | Runs sampled-XP support-envelope go/no-go checks from catalog coverage, rights status, and validation metrics |
| `build_spcc_hf_dataset.py` | Stages validated SPCC catalog packs into a Hugging Face dataset-ready directory with dataset card, mirror manifest, checksums, and upload command |
| `verify_spcc_hf_dataset.py` | Verifies a hosted or local SPCC Hugging Face-style dataset manifest, sidecar checksums, and optional `.anxp` chunk checksums |
| `verify_spcc_catalog_mirror_config.py` | Verifies an app-facing `spcc_catalog_mirrors.json` descriptor config and the referenced SPCC catalog manifests |
| `estimate_spcc_catalog_size.py` | Writes reproducible SPCC TAP-count/size-estimate transcripts |
| `fetch_spcc_gaia_validation_fixture.py` | Fetches tiny private Gaia TAP + XP sampled validation inputs and redacted provenance transcripts |
| `import_spcc_response_database.py` | Converts a pinned SPCC response database checkout or ZIP archive into an AfterNight response source manifest and curve CSV work area |
| `validate_spcc_response_source_manifest.py` | Validates response source manifests before normalization, including strict production provenance and placeholder checks |
| `normalize_spcc_response_repository.py` | Normalizes licensed response curves and white references into an AfterNight response repository |
| `validate_spcc_response_repository.py` | Validates normalized response repositories from source-tree or deployed/runtime paths |
| `build_spcc_response_repository_dataset.py` | Stages validated response repositories into a hosted dataset-ready directory with descriptor config and dataset card |
| `verify_spcc_response_repository_config.py` | Verifies an app-facing response repository descriptor config, publication/update policy, and referenced response assets |
| `build_spcc_real_image_validation_entries.py` | Hashes reviewed real-image and external-comparison artifacts into ready-to-merge validation-manifest entries |
| `merge_spcc_validation_manifest_entries.py` | Merges generated SPCC evidence entries into the validation-data manifest and validates the result |
| `write_spcc_hf_upload_instructions.py` | Writes final SPCC Hugging Face upload, hosted verification, evidence merge, and release-readiness commands from staged datasets |
| `check_spcc_release_readiness.py` | Runs the final SPCC production release-readiness gate across validation evidence and descriptor configs |
| `premake5` / `premake5.exe` | Premake5 build system binary (used by `build/scripts/`) |

---

## SPCC Catalog and Response Tooling

The Phase 3 SPCC tools are stdlib-only and designed to run deterministic fixture paths without multi-GB Gaia downloads:

```bash
python3 tools/generate_spcc_catalog.py catalogs/spcc_fixture --fixture --fixture-source-count 6
python3 tools/validate_spcc_catalog.py catalogs/spcc_fixture --sample-count 3
python3 tools/check_spcc_support_envelope.py catalogs/spcc_fixture --validation-metrics spcc_support_metrics.json --output-json spcc_go_no_go_report.json --manifest-entry-json spcc_support_manifest_entry.json
python3 tools/build_spcc_hf_dataset.py catalogs/spcc_hf_smoke_dataset catalogs/spcc_fixture --clean
python3 tools/verify_spcc_hf_dataset.py catalogs/spcc_hf_smoke_dataset/spcc_dataset_manifest.json --base-url-override "$(pwd)/catalogs/spcc_hf_smoke_dataset" --chunk-policy first
python3 tools/verify_spcc_catalog_mirror_config.py spcc_catalog_mirrors.json --chunk-policy head
python3 tools/estimate_spcc_catalog_size.py --counts-fixture counts.json --output-transcript spcc_counts.json
python3 tools/fetch_spcc_gaia_validation_fixture.py catalogs/spcc_validation/tiny_gaia_xp_sampled --source-count 3 --probe-datalink
python3 tools/fetch_spcc_gaia_validation_fixture.py catalogs/spcc_validation/m31_region --source-count 120 --artifact-prefix m31_region --region-name m31-core --ra-deg 10.6847083 --dec-deg 41.26875 --radius-deg 1.5 --mag-min 9 --mag-max 16 --min-bp-rp-snr 20
python3 tools/generate_spcc_catalog.py catalogs/spcc_validation/m31_region/catalog --input-jsonl catalogs/spcc_validation/m31_region/m31_region_spcc_sources.jsonl --source-transcript catalogs/spcc_validation/m31_region/gaia_validation_transcript.json --regional-request catalogs/spcc_validation/m31_region/spcc_regional_request.json --manifest-id spcc-gaia-dr3-xp-sampled-m31-region-20260512 --preset sampled-region
python3 tools/export_spcc_gaia_xp_metadata.py catalogs/spcc_production/full_sampled_2026a/metadata --tap-official --healpix8-start 0 --healpix8-end 786432 --healpix8-step 64
python3 tools/download_spcc_gaia_xp_sampled_files.py catalogs/spcc_production/full_sampled_2026a/xp_sampled
python3 tools/export_spcc_gaia_full_xp_sampled_sources.py catalogs/spcc_production/full_sampled_2026a/prejoined --metadata-manifest catalogs/spcc_production/full_sampled_2026a/metadata/spcc_gaia_xp_metadata_export_manifest.json --xp-sampled-manifest catalogs/spcc_production/full_sampled_2026a/xp_sampled/spcc_gaia_xp_sampled_download_manifest.json --partition-count 4096
python3 tools/build_spcc_full_xp_sampled_catalog.py catalogs/spcc_production/full_sampled_2026a/catalog --input-manifest catalogs/spcc_production/full_sampled_2026a/prejoined/spcc_full_xp_sampled_source_export_manifest.json --manifest-id spcc-gaia-dr3-xp-sampled-full-2026b --cap-per-healpix8 127 --rights-review-status private-local
python3 tools/approve_spcc_catalog_rights.py catalogs/spcc_production/full_sampled_2026a/catalog --reviewer "AfterNight Gaia DR3 rights review"
python3 tools/import_spcc_response_database.py path/to/spcc-response-database-or.zip spcc_response_source --repository-id afternight-spcc-response-baseline --source-revision <pinned-commit> --review-status approved-for-managed-download --reviewer "<reviewer>" --skip-osc-filter-lpf-combinations
python3 tools/validate_spcc_response_source_manifest.py spcc_response_source/response_source_manifest.json --strict-production-review --report-json response_source_audit.json
python3 tools/normalize_spcc_response_repository.py spcc_response_source/response_source_manifest.json resources/spcc_responses --strict-production-review
python3 tools/validate_spcc_response_repository.py resources/spcc_responses --runtime-root
python3 tools/build_spcc_response_repository_dataset.py spcc_response_dataset resources/spcc_responses --clean --validation-manifest-entries-json spcc_response_manifest_entries.json
python3 tools/verify_spcc_response_repository_config.py spcc_response_repositories.json
python3 tools/build_spcc_real_image_validation_entries.py real_image_validation_source_manifest.json spcc_real_image_manifest_entries.json --strict-production-review
python3 tools/merge_spcc_validation_manifest_entries.py docs/dev/spcc/spcc-validation-data-manifest.json spcc_response_manifest_entries.json spcc_support_manifest_entry.json spcc_real_image_manifest_entries.json spcc_packaged_qa_manifest_entry.json --output docs/dev/spcc/spcc-validation-data-manifest.merged.json --release-ready
python3 tools/write_spcc_hf_upload_instructions.py --catalog-dataset-dir spcc_catalog_dataset --response-dataset-dir spcc_response_dataset --response-source-manifest response_source_manifest.json --real-image-validation-source-manifest real_image_validation_source_manifest.json --packaged-qa-manifest-entry spcc_packaged_qa_manifest_entry.json --output-markdown spcc_hf_upload_instructions.md
python3 tools/check_spcc_release_readiness.py --validation-manifest docs/dev/spcc/spcc-validation-data-manifest.json --catalog-mirror-config spcc_catalog_mirrors.json --response-repository-config spcc_response_repositories.json --response-source-manifest response_source_manifest.json --real-image-validation-source-manifest real_image_validation_source_manifest.json
```

Non-empty `spcc_response_repositories.json` configs must include `publication_policy` with `channel` (`stable` or
`preview`), `published_at_utc`, and `update_policy: "immutable-versioned"`. Each repository/deployment-scope channel
must expose exactly one `release_status: "current"` descriptor, current descriptors must be recommended, and superseded
descriptors must remain addressable with `superseded_by` pointing at the current descriptor.

`build_spcc_response_repository_dataset.py` does not upload by itself. It creates a local hosted-dataset staging
folder with copied response repositories under `responses/<repository_id>/<version>/`, a dataset card,
`spcc_response_dataset_manifest.json`, and app-facing `spcc_response_repositories.json`. Pass
`--direct-base-url <local-or-hosted-base>` for local verifier smoke before uploading. Pass
`--validation-manifest-entries-json <path>` to write ready-to-merge `response_repositories` evidence entries for
`docs/dev/spcc/spcc-validation-data-manifest.json`.

`validate_spcc_response_source_manifest.py --report-json <path>` writes a structured
`spcc-response-source-manifest-audit-v1` report with pass/fail state, unresolved placeholder paths, component/reference
counts, and curve-file readiness. Use it while filling `docs/dev/spcc/spcc-response-baseline-2026a-source-manifest.template.json`.

`import_spcc_response_database.py` is the practical response-baseline intake path for SPCC sensor/filter data adapted from
the credited upstream SPCC response database. Run it against a local checkout/export or GitLab ZIP archive of
`free-astro/siril-spcc-database` pinned to a specific commit. ZIP input is preferred on Windows because it preserves
source files whose names differ only by case. The importer writes `response_source_manifest.json` plus `curves/*.csv`
with neutral `afternight.spcc.*` runtime IDs, preserves the upstream source path, data quality marker, data source, GPLv3
attribution, and generated profile channel mappings, then feeds the existing validator and normalizer. Use an approved
review status only after GPL/provenance review is recorded; otherwise keep the generated manifest as
`license-review-needed` or `private-local`.

`build_spcc_real_image_validation_entries.py` expects a reviewed
`spcc-real-image-validation-source-v1` manifest whose file paths stay under the manifest directory. Start from
`docs/dev/spcc/spcc-real-image-validation-source-manifest.template.json`, replace every TODO with reviewed provenance and run
metadata, and add the referenced image/report/comparison files. In strict production mode the builder rejects
placeholders and non-approved review states, hashes the referenced image/comparison files, and writes ready-to-merge
`real_image_reference_sets` and `external_comparison_outputs` entries. The final release-readiness gate rebuilds those
entries and compares them with `docs/dev/spcc/spcc-validation-data-manifest.json`.

`merge_spcc_validation_manifest_entries.py` accepts generated response-entry wrappers, generated real-image-entry
wrappers, single support-envelope entry objects, and packaged-QA entry wrappers. It replaces matching IDs, appends new
IDs, updates `updated_at_utc`, and validates the merged manifest before writing.

`write_spcc_hf_upload_instructions.py` is the handoff tool to run after production catalog and response datasets have
been staged locally. It refuses empty app-facing descriptor configs and writes exact local verification, `hf upload`,
hosted verification, evidence merge, and final release-readiness commands.

`generate_spcc_catalog.py --tap-official` is intentionally guarded until an approved Gaia/DataLink export path is supplied. Use `--input-jsonl` for approved local source/spectrum exports, or `--fixture` for tests.
When using JSONL from `fetch_spcc_gaia_validation_fixture.py`, pass `--source-transcript` to preserve the official TAP
query and XP bulk-file provenance in `source_count_transcript.json`.
For regional-cache packs, pass an app-compatible `spcc-regional-cache-request-v1` JSON via `--regional-request`. The
generator writes `regional_cache_request.json`, stamps descriptor-compatible `coverage` into `catalog_meta.json` and
`coverage_summary.json`, and `build_spcc_hf_dataset.py` carries that coverage into staged dataset and mirror manifests.

`export_spcc_gaia_xp_metadata.py` creates the metadata side of the production full-sky join. For production, use
`--tap-official` so the tool queries the official Gaia TAP service by HEALPix-8 source-id ranges and writes resumable
CSV.GZ parts plus `spcc_gaia_xp_metadata_export_manifest.json`. If interrupted, continue with:

```bash
python3 tools/export_spcc_gaia_xp_metadata.py catalogs/spcc_production/full_sampled_2026a/metadata --resume
```

`download_spcc_gaia_xp_sampled_files.py` creates the spectra side of the production full-sky join. It lists the
official Gaia DR3 XP sampled bulk files, downloads them into the output directory, and writes
`spcc_gaia_xp_sampled_download_manifest.json`. If interrupted, continue with:

```bash
python3 tools/download_spcc_gaia_xp_sampled_files.py catalogs/spcc_production/full_sampled_2026a/xp_sampled --resume
```

`export_spcc_gaia_full_xp_sampled_sources.py` is the production-scale prejoin step. Pass the metadata manifest with
`--metadata-manifest` and the XP sampled download manifest with `--xp-sampled-manifest`. The exporter partitions both
sides into durable work files, joins by `source_id`, writes `parts/*.jsonl`, and emits
`spcc_full_xp_sampled_source_export_manifest.json`. If interrupted, continue with:

```bash
python3 tools/export_spcc_gaia_full_xp_sampled_sources.py catalogs/spcc_production/full_sampled_2026a/prejoined --resume
```

`build_spcc_full_xp_sampled_catalog.py` is the production-scale catalog packer. It consumes either repeated
`--input-jsonl` files or the exporter's `--input-manifest`, keeps the brightest `--cap-per-healpix8` sources per order-8
HEALPix tile, writes chunked `.anxp` files, and emits `catalog_meta.json`, `chunk_manifest.json`,
`coverage_summary.json`, `source_id_index.json`, and `source_count_transcript.json`. Production `local-full` builds must
declare every order-3 parent chunk and every order-6 tile (768 chunks / 49,152 tiles with the default format). The
builder, validator, dataset stager, and rights-approval tool reject Gaia-derived managed-download packs that are partial.
Use `--allow-partial-full-sky` only for small fixture/dev catalogs. If a published managed pack was incomplete, rebuild
from complete inputs and publish a new immutable `manifest_id` rather than replacing the old one in place. If a long run
is interrupted,
continue with:

```bash
python3 tools/build_spcc_full_xp_sampled_catalog.py catalogs/spcc_production/full_sampled_2026a/catalog --resume
```

Use `--clean` on either tool only when intentionally discarding that output directory and its resumable work state.

`check_spcc_support_envelope.py --manifest-entry-json` writes a ready-to-review
`support_envelope_reports` entry for `docs/dev/spcc/spcc-validation-data-manifest.json`, including SHA-256 values for the
validation metrics and go/no-go report. Use it after real-image validation metrics exist; coverage-only runs still fail
as `validation_missing`.

`build_spcc_hf_dataset.py` does not upload by itself. It creates the local Hugging Face dataset folder, including
`README.md`, `.gitattributes`, `spcc_dataset_manifest.json`, `spcc_mirror_manifest.json`,
`spcc_catalog_mirrors.json`, and copied catalog packs under
`spcc_gaia_dr3_xp_sampled/<access-profile>/<manifest-id>/`. The app-facing `spcc_catalog_mirrors.json` includes only
catalogs with approved managed-use rights; private-local packs remain staged but hidden from Catalogs/Data descriptor
discovery. Non-empty catalog descriptor configs include `publication_policy` with `channel` (`stable` or `preview`),
`published_at_utc`, and `update_policy: "immutable-versioned"`. Catalog descriptors must declare
`release_status: "current"` or `"superseded"`; current descriptors are offered by the app, while superseded descriptors
remain in the config for replacement provenance through `superseded_by` links. Pass
`--direct-base-url <local-or-hosted-base>` for local descriptor-verifier smoke before uploading. If the staged config
keeps its hosted mirror URL, pass `--base-url-override <staged-dataset-dir>` to
`verify_spcc_catalog_mirror_config.py` for the same pre-upload smoke. Public-production mode is blocked unless the
catalog manifest records approved Gaia redistribution and mirror-hosting review; use
`approve_spcc_catalog_rights.py` to stamp that reviewed state before public staging.
