# SPCC Production Release Evidence Checklist

Use this checklist to take SPCC to a release-ready production path. The final gate is:

```powershell
python tools\check_spcc_release_readiness.py `
  --validation-manifest docs\dev\spcc\spcc-validation-data-manifest.json `
  --catalog-mirror-config <verified-spcc_catalog_mirrors.json> `
  --response-repository-config <verified-spcc_response_repositories.json> `
  --response-source-manifest <reviewed-response-source-manifest.json> `
  --real-image-validation-source-manifest <reviewed-real-image-validation-source-manifest.json> `
  --chunk-policy head
```

That command must pass before SPCC is declared production-ready.

## 1. Response Baseline

- Preferred Siril-style intake: clone/export `free-astro/siril-spcc-database`, or use its GitLab ZIP archive, at a
  pinned commit. Complete the
  GPL/provenance review, then convert it into the reviewed response-source work area:

```powershell
python tools\import_spcc_response_database.py <spcc-response-database-dir-or-zip> <response-source-dir> `
  --repository-id afternight-spcc-response-baseline `
  --source-revision <pinned-siril-spcc-database-commit> `
  --review-status approved-for-managed-download `
  --reviewer "<reviewer>" `
  --skip-osc-filter-lpf-combinations
```

- Alternative manual intake: copy `docs/dev/spcc/spcc-response-baseline-2026a-source-manifest.template.json` to the
  reviewed response-source work area, replace every `TODO`, and add the referenced CSV curves under `curves/`.
- Normalize and validate:

```powershell
python tools\validate_spcc_response_source_manifest.py <response-source-dir>\response_source_manifest.json --strict-production-review --report-json <response-source-audit.json>
python tools\normalize_spcc_response_repository.py <response-source-dir>\response_source_manifest.json <normalized-response-repo> --strict-production-review
python tools\validate_spcc_response_repository.py <normalized-response-repo> --runtime-root
python tools\validate_spcc_response_repository.py <normalized-response-repo> --strict-production-review
```

- Stage and verify hosted descriptors:

```powershell
python tools\build_spcc_response_repository_dataset.py <response-dataset-dir> <normalized-response-repo> --clean --validation-manifest-entries-json <response-manifest-entries.json>
python tools\verify_spcc_response_repository_config.py <response-dataset-dir>\spcc_response_repositories.json --no-token
```

- Merge the generated `response_repositories` entries from `<response-manifest-entries.json>` into
  `docs/dev/spcc/spcc-validation-data-manifest.json`.

Current local 2026a response baseline evidence:

- Source revision: `free-astro/siril-spcc-database` commit `65283d29962d72134378852a3a0be643c150d129`.
- Preserved source archive: `catalogs/spcc_production/source_archives/siril-spcc-database-65283d29962d72134378852a3a0be643c150d129.zip`.
- Reviewed source work area: `catalogs/spcc_production/response_source_afternight_2026a`.
- Source audit report: `catalogs/spcc_production/response_source_afternight_2026a_audit.json`.
- Normalized runtime repository: `catalogs/spcc_production/response_repository_afternight_2026a`.
- Upload-ready Hugging Face dataset staging directory: `catalogs/spcc_production/response_dataset_afternight_2026a`.
- Hosted response dataset target: `https://huggingface.co/datasets/emruiz81/afternight-spcc-responses/resolve/main/spcc_response_repositories.json`.
- Validation-manifest entries: `catalogs/spcc_production/response_manifest_entries_afternight_2026a.json`.
- Validation manifest: `afternight-spcc-response-baseline-2026a` is merged into
  `docs/dev/spcc/spcc-validation-data-manifest.json`.
- Counts: 245 response components, 145 white references, 2,342 generated throughput profiles.
- Local checks already passed: strict source-manifest validation, runtime-root repository validation, strict production
  repository validation.
- Hosted verification is pending upload of the neutralized response dataset. Before release, rerun:
  `python tools\verify_spcc_response_repository_config.py https://huggingface.co/datasets/emruiz81/afternight-spcc-responses/resolve/main/spcc_response_repositories.json`
  and record the passing result here.

## 2. Gaia XP Catalog Candidate

- Record Gaia redistribution/citation review in `catalog_meta.json`.
- Build at least one full local sampled-XP candidate and at least one regional-cache candidate.
- Validate and stage:

```powershell
python tools\validate_spcc_catalog.py <catalog-dir> --strict-production-review
python tools\build_spcc_hf_dataset.py <catalog-dataset-dir> <catalog-dir> --strict-production-review --clean
python tools\verify_spcc_catalog_mirror_config.py <catalog-dataset-dir>\spcc_catalog_mirrors.json --chunk-policy head
```

2026a full-sky catalog evidence (superseded by `spcc-gaia-dr3-xp-sampled-full-2026b`). `<production-root>` stands
for the local directory the production run wrote to; the recorded digests identify the outputs, not the path:

- Metadata export: `<production-root>\full_sampled_2026a\metadata`, 3,072 metadata CSV.GZ parts,
  4,608,566 rows, manifest SHA-256
  `3d626549d09911b38f4c86673f3d8b471d104b6908eb6fea7106533b5f99b395`.
- XP sampled spectra download: `<production-root>\full_sampled_2026a\xp_sampled`, 3,386 official Gaia DR3
  `XpSampledMeanSpectrum_*.csv.gz` files, 106.569 GiB, manifest SHA-256
  `619b540850ba58a367cf79e2177437a78b33a88ce89d91fcc38f2ae5f5b25685`.
- Prejoin output: `<production-root>\full_sampled_2026a\prejoined`, 32 JSONL parts, 4,608,566 joined rows,
  manifest SHA-256 `74384e0e6e5ac7a81890c26ec7a5d8bf7c101030956cb0af179992dceb3ff3ff`.
- Runtime catalog: `<production-root>\full_sampled_2026a\catalog`, manifest ID
  `spcc-gaia-dr3-xp-sampled-full-2026a`, 192 `.anxp` chunks, 4,601,105 records, `approved-for-managed-download`
  rights status, `catalog_meta.json` SHA-256
  `0bc2fc504a9fc94a978d89a270559d41c03b3c1169a272637371efea6bea0003`.
- Gaia DR3 rights approval was recorded on 2026-05-14 with:
  `python tools\approve_spcc_catalog_rights.py <production-root>\full_sampled_2026a\catalog --reviewer "AfterNight Gaia DR3 rights review"`.
  The catalog records ESA/Gaia/DPAC credit, Gaia DR3 citation requirements, and `approved-public` mirror hosting policy.
- Local validation passed:
  `python tools\validate_spcc_catalog.py <production-root>\full_sampled_2026a\catalog --sample-count 64 --strict-production-review`.
- Public-production Hugging Face staging dataset:
  `<production-root>\full_sampled_2026a\hf_dataset_public`, uploaded to
  `https://huggingface.co/datasets/emruiz81/afternight-spcc-catalogs/commit/e7cc3666ed33854cd04a24d72e3f0cdb2a74ee30`.
- Hosted dataset verification passed on 2026-05-14:
  `python tools\verify_spcc_hf_dataset.py https://huggingface.co/datasets/emruiz81/afternight-spcc-catalogs/resolve/main/spcc_dataset_manifest.json --chunk-policy first`
  returned `SPCC hosted dataset verification passed`.
- Hosted app-facing catalog mirror config verification passed on 2026-05-14:
  `python tools\verify_spcc_catalog_mirror_config.py https://huggingface.co/datasets/emruiz81/afternight-spcc-catalogs/resolve/main/spcc_catalog_mirrors.json --chunk-policy first`
  returned `SPCC catalog mirror config verification passed: 1 descriptor(s)`.
- Validation manifest: `full-sampled-xp-managed-download-2026a` is merged into
  `docs/dev/spcc/spcc-validation-data-manifest.json`.

Remaining blockers before SPCC is fully production-ready: support-envelope metrics, real-image validation evidence,
external comparison evidence, and packaged QA.

## 3. Real-Image Validation

- Copy `docs/dev/spcc/spcc-real-image-validation-source-manifest.template.json` to the reviewed validation-data
  work area.
- Replace every `TODO`, set production-approved rights statuses, and add the referenced input/output/report files.
- Add licensed/checksummed broadband reference sets covering small, medium, and wide fields.
- Run AfterNight SPCC against each set using the reviewed catalog and response repository.
- Record accepted samples, gain uncertainty, residuals, sparse-field warnings, and output checksums.
- Generate and merge the real-image entries:

```powershell
python tools\build_spcc_real_image_validation_entries.py <reviewed-real-image-validation-source-manifest.json> <real-image-validation-manifest-entries.json> --strict-production-review
```

- Merge the generated `real_image_reference_sets` entries into `docs/dev/spcc/spcc-validation-data-manifest.json`.

## 4. External Comparisons

- Run the external reference implementations when available, and name each in the manifests by a generic tool id
  (`reference-a`, `reference-b`, ...), never by product.
- Record tool version, command/run notes, output checksums, and comparison tolerances.
- Use the same generated `<real-image-validation-manifest-entries.json>` file from section 3.
- Merge the generated `external_comparison_outputs` entries into `docs/dev/spcc/spcc-validation-data-manifest.json`.
- The final release-readiness command rebuilds entries from the reviewed source manifest and compares them with the
  validation manifest.

## 5. Support Envelope

- Write the validation metrics file for the reviewed catalog manifests.
- Generate the go/no-go report and manifest entry:

```powershell
python tools\check_spcc_support_envelope.py <catalog-dir> --validation-metrics <metrics.json> --output-json <go-no-go.json> --manifest-entry-json <manifest-entry.json> --manifest-entry-id sampled-xp-support-envelope-2026a
```

- Review and merge the generated `support_envelope_reports` entry into
  `docs/dev/spcc/spcc-validation-data-manifest.json`.
- Publish the final v1 support envelope in the user/developer docs.

## 6. Packaged QA

- After the production catalog and response datasets are staged locally, generate the upload handoff:

```powershell
python tools\write_spcc_hf_upload_instructions.py `
  --catalog-dataset-dir <catalog-dataset-dir> `
  --response-dataset-dir <response-dataset-dir> `
  --response-source-manifest <reviewed-response-source-manifest.json> `
  --real-image-validation-source-manifest <reviewed-real-image-validation-source-manifest.json> `
  --response-manifest-entries <response-manifest-entries.json> `
  --support-envelope-manifest-entry <support-envelope-manifest-entry.json> `
  --real-image-manifest-entries <real-image-validation-manifest-entries.json> `
  --packaged-qa-manifest-entry <packaged-qa-manifest-entry.json> `
  --merged-validation-manifest <merged-validation-manifest.json> `
  --output-markdown <spcc-hf-upload-instructions.md>
```

- Merge generated evidence entries and validate the release manifest:

```powershell
python tools\merge_spcc_validation_manifest_entries.py `
  docs\dev\spcc\spcc-validation-data-manifest.json `
  <response-manifest-entries.json> `
  <support-envelope-manifest-entry.json> `
  <real-image-validation-manifest-entries.json> `
  <packaged-qa-manifest-entry.json> `
  --output <merged-validation-manifest.json> `
  --release-ready
```

- Enable the verified catalog and response descriptor configs.
- Smoke hosted downloads, repair, resume, cancellation, install markers, and eviction.
- Verify packaged runtime discovery/checksum behavior for response assets.
- Run final 1080p/720p packaged-app screenshot/manual QA.
- Copy `docs/dev/spcc/spcc-packaged-qa-manifest-entry.template.json`, replace every `TODO`, set all completed checklist
  values to `true`, and merge the generated/filled `packaged_qa_reports` entry.
- Re-run the release-readiness command at the top of this file.
