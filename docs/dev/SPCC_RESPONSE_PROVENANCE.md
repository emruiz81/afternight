# SPCC Response Curve Provenance And Licensing Rules

Rules for adding, reviewing, validating, deploying, and updating Spectrophotometric Color Calibration response
repositories.

SPCC response data includes sensor/channel responses, filter curves, LPF/window/optics curves, optional future
atmospheric curves, and white-reference spectra. These assets directly change fitted color gains, so production SPCC
must treat them as versioned scientific data, not informal presets.

Related guides:

- `docs/dev/SPCC_USER_WORKFLOW.md` for the process-facing user workflow.
- `docs/dev/SPCC_CATALOG_REQUIREMENTS.md` for catalog-side requirements and support-envelope assumptions.
- `docs/dev/SPCC_CATALOG_MAINTENANCE.md` for catalog generation, validation, provenance, and release gates.

## Repository Shape

A normalized response repository is rooted at `response_meta.json` and currently uses these schemas:

- Repository manifest: `spcc-response-repository-v1`
- Asset payloads: `spcc-response-asset-v1`

Use the normalizer to convert reviewed source manifests and tabular curve data into runtime JSON payloads:

```powershell
python tools\normalize_spcc_response_repository.py <input-manifest.json> <output-response-repository>
```

For a Siril-style response baseline, first convert a pinned checkout/export or GitLab ZIP archive of
`free-astro/siril-spcc-database` into the same source-manifest work area:

```powershell
python tools\import_spcc_response_database.py <spcc-response-database-dir-or-zip> <response-source-dir> `
  --repository-id afternight-spcc-response-baseline `
  --source-revision <pinned-siril-spcc-database-commit> `
  --review-status approved-for-managed-download `
  --reviewer "<reviewer>" `
  --skip-osc-filter-lpf-combinations
```

The importer writes `<response-source-dir>\response_source_manifest.json` plus `curves\*.csv`. Runtime component,
white-reference, and profile IDs use the neutral `afternight.spcc.*` namespace, while provenance fields preserve Siril
source paths, data quality markers, data sources, GPLv3 attribution, and channel-aware throughput profiles. Keep the generated
manifest as `license-review-needed` or `private-local` until the GPL/provenance review for AfterNight-managed download
or bundling has been recorded.

To build a reviewed baseline by hand, start from
`docs/dev/spcc/spcc-response-baseline-2026a-source-manifest.template.json` and replace every `TODO` before normalization.
That template includes the production provenance fields preserved by the normalizer and enforced by strict validation.
Validate the reviewed source manifest before generating normalized runtime assets:

```powershell
python tools\validate_spcc_response_source_manifest.py <input-manifest.json> --strict-production-review --report-json <response-source-audit.json>
python tools\normalize_spcc_response_repository.py <input-manifest.json> <output-response-repository> --strict-production-review
```

The audit report uses schema `spcc-response-source-manifest-audit-v1` and records pass/fail state, unresolved
placeholder paths, component/reference/profile counts, and curve-file readiness so the reviewed-data intake can be
checked before normalization.

Validate the normalized repository before it is selected by the app or staged for packaging:

```powershell
python tools\validate_spcc_response_repository.py <output-response-repository>
python tools\validate_spcc_response_repository.py <output-response-repository> --runtime-root
python tools\validate_spcc_response_repository.py <output-response-repository> --strict-production-review
```

`--runtime-root` verifies the repository declares the `deployed-runtime` scope expected by packaged builds.
`--strict-production-review` requires repository and asset review states to be production-approved.

## Required Manifest Metadata

Every response repository manifest must record:

- `repository_id`
- `version`
- `rights.review_status`
- `components`
- `white_references`
- `throughput_profiles`
- `deployment_scopes`
- generator name/version and generation timestamp

Every curve or white-reference asset must record:

- stable asset ID and version;
- role, such as sensor/channel response, filter, LPF/window, optics, atmosphere, or white reference;
- wavelength unit, currently `nm`;
- value semantics, such as `dimensionless-throughput` or `relative-sed`;
- interpolation rule, currently linear unless a future schema says otherwise;
- out-of-range behavior, currently zero outside the tabulated wavelength range;
- normalization rule, usually preserving the relative curve shape;
- review status;
- license name;
- source URL, DOI, or citation;
- canonical sample-data SHA-256;
- generated payload file SHA-256 recorded in `response_meta.json`.
- production strict validation additionally requires each asset payload to preserve non-empty `license_name`,
  `source_url`, `source_citation`, `attribution_text`, and `transformation_notes`.

Missing license names, missing source URLs, unknown units, mismatched checksums, or ambiguous value semantics block
production review. A private or unreviewed repository can be selected, but SPCC readiness refuses any profile built from
private or unreviewed assets, so it cannot drive a run.

## Review States

Use these states consistently in repository and asset metadata:

| State | Meaning |
| --- | --- |
| `private-local` | Local development or user-private data. It can be selected and inspected, but it is not production-approved and SPCC refuses profiles built from it. |
| `license-review-needed` | Metadata or terms are incomplete. Do not package, mirror, or recommend it. |
| `approved-for-bundling` | Reviewed for inclusion in AfterNight-distributed packages. |
| `approved-for-managed-download` | Reviewed for AfterNight-managed download/cache distribution. |
| `blocked` | Known license, provenance, quality, or technical reason prevents production use. |

Production response repositories must set the repository review state and every asset payload review state to either
`approved-for-bundling` or `approved-for-managed-download`, depending on the deployment mode.

## Licensing Rules

Before an asset becomes bundled or managed-download data, record the review outcome in the source manifest and normalized
repository:

- license name and license URL when available;
- source URL, DOI, datasheet, paper, or vendor documentation URL;
- required attribution/citation text;
- redistribution permission for packaged app builds;
- permission to mirror or serve through AfterNight-managed downloads;
- permission to transform, normalize, resample, or combine the curve into a throughput profile;
- restrictions such as non-commercial terms, no-derivatives terms, export restrictions, account-gated downloads, or
  attribution placement requirements;
- reviewer/date/tooling notes sufficient to reproduce the decision.

Do not bundle or mirror assets when the source is unknown, the license is absent, redistribution is unclear, the data was
copied from an unreviewed third-party mirror, or the source terms forbid the intended deployment. A user may import such
data into a private repository, but the app must keep that repository separate from reviewed production roots and report
the private/unreviewed status.

## Provenance Rules

Response data must remain reproducible from the reviewed source material:

- Keep the source manifest used by `normalize_spcc_response_repository.py` under review with the normalized repository
  or release artifact records.
- Preserve original source filenames, URLs, versions, and acquisition dates outside the normalized payload when possible.
- Record any unit conversion, wavelength resampling, clipping, smoothing, or normalization choice.
- Treat sample-data SHA-256 as the identity of the numerical curve. If samples change, create a new asset version and a
  new repository version.
- Treat throughput profiles as immutable combinations of component IDs, component versions, white-reference IDs, channel
  compatibility tags, and synthetic-photometry assumptions.
- Do not silently replace a curve behind an existing asset ID/version, even if the new source is better.

The app may save manual camera/filter mappings, but saved mappings must point to immutable profile identity, repository
ID, and alias-registry version. A repository update that changes aliases or profile identity must require revalidation of
the mapping.

## Throughput Profile Rules

A throughput profile must declare:

- response-profile ID and version;
- referenced component IDs;
- referenced white-reference ID;
- compatible channel bases;
- camera/filter aliases, when reviewed;
- whether the profile models raw white balance;
- whether the profile models camera matrix conversion.

The SPCC process must not guess missing response components or substitute a default profile. Metadata aliases can
preselect only exact, unambiguous, reviewed mappings. Ambiguous or missing mappings require explicit user selection.

## Deployment Rules

Keep response roots separated by provenance and mutability:

- source-tree fixtures for development and tests;
- bundled read-only runtime repositories;
- installer/AppImage packaged repositories;
- user-private repositories;
- managed-cache repositories installed by Catalogs & Data.

Production smoke checks must validate the deployed runtime copy, not only the source-tree fixture. A packaged repository
must have the same `response_meta.json`, asset payload checksums, deployment scopes, and review states that were reviewed
before packaging.

## Managed Descriptor Config

Managed response repositories are advertised through a separate descriptor config, not through the catalog mirror config.
The app parses this config for approved descriptor discovery, Catalogs/Data status reporting, and the
checksum-verified managed-cache installer. App-side discovery reads the config from the app data location and only
offers `release_status = current` descriptors for normal managed installs; superseded descriptors remain in the hosted
config for audit/update provenance. The config has this shape:

```json
{
  "schema_version": "spcc-response-repository-config-v1",
  "publication_policy": {
    "channel": "stable",
    "published_at_utc": "2026-05-11T00:00:00Z",
    "update_policy": "immutable-versioned"
  },
  "mirrors": ["https://example.invalid/afternight-spcc-responses/resolve/main"],
  "response_repositories": [
    {
      "id": "spcc-response-baseline-2026a",
      "repository_id": "spcc-response-baseline",
      "version": "2026a",
      "display_name": "AfterNight SPCC Response Baseline 2026a",
      "description": "Reviewed sensor/filter/white-reference response repository.",
      "manifest_path": "responses/spcc-response-baseline/2026a/response_meta.json",
      "rights_review_status": "approved-for-managed-download",
      "deployment_scope": "managed-cache",
      "response_meta_sha256": "<sha256>",
      "release_status": "current",
      "recommended": true
    }
  ]
}
```

Descriptor admission is intentionally narrow:

- non-empty configs must include `publication_policy.channel = stable|preview`, `published_at_utc` as an ISO-8601 UTC
  timestamp, and `update_policy = immutable-versioned`;
- `rights_review_status` must be `approved-for-bundling` or `approved-for-managed-download`.
- `deployment_scope` must be `managed-cache`, `deployed-runtime`, `installer`, or `appimage`, and the referenced
  `response_meta.json` must declare that scope.
- relative `manifest_path` values must stay under the configured mirror and end in `response_meta.json`.
- descriptor `repository_id`, `version`, review state, and optional `response_meta_sha256` must match the hosted
  manifest.
- every repository/deployment-scope channel must have exactly one `release_status = current` descriptor; current
  descriptors must be `recommended = true`, and superseded descriptors must set `release_status = superseded`,
  `recommended = false`, and `superseded_by` to the current descriptor ID.
- every component and white-reference payload must have approved review status, license/source metadata, valid
  monotonic curve samples, matching data checksums, and matching payload file checksums.

Before enabling a non-empty managed response config, run:

```powershell
python tools\build_spcc_response_repository_dataset.py <dataset-dir> <reviewed-response-repo> --clean --validation-manifest-entries-json <response-manifest-entries.json>
python tools\verify_spcc_response_repository_config.py <path-or-url-to-spcc_response_repositories.json>
```

Merge the generated `response_repositories` entries into `docs/dev/spcc/spcc-validation-data-manifest.json` so the final
release-readiness check can tie the reviewed response repository to the validation evidence set.
Use `tools/merge_spcc_validation_manifest_entries.py` when combining response evidence with support-envelope,
real-image, and external-comparison evidence so the merged validation manifest is validated before it is written.

Private repositories may still be selected locally through the existing repository picker, but they must not be listed in
managed descriptor configs. App-side descriptor discovery intentionally mirrors the runtime admission rules for schema
version, approved review states, safe `response_meta.json` paths, repository identity, deployment scope, optional
manifest checksum syntax, duplicate identity rejection, and non-negative size metadata. The standalone verifier is the
deeper publication gate: it also enforces publication/update policy, fetches hosted manifests/assets, and validates
supersession links before a config is enabled. The managed installer then revalidates
manifest identity/review/deployment metadata, verifies the optional manifest SHA-256, downloads referenced component and
white-reference payloads only when their manifest asset review states are approved, verifies payload file SHA-256
values, and atomically promotes the completed versioned repository under the managed response cache.

## Update Rules

Create a new repository version when any of these change:

- asset samples, source data, or data checksum;
- license/provenance review state;
- throughput-profile composition;
- white-reference choice;
- aliases or alias-registry version;
- value semantics, unit conversion, normalization, interpolation, or out-of-range behavior;
- deployment scope.

Do not mutate published response payloads in place. Stage the new repository, validate it, run focused response
repository tests, then update Catalogs/Data descriptors or packaging manifests to point at the new reviewed version.
Managed descriptor updates must keep the old descriptor addressable as `release_status = superseded`, point its
`superseded_by` field at the current descriptor, and mark exactly one current descriptor as recommended for each
repository/deployment-scope channel. Catalogs & Data only offers `release_status = current` descriptors for managed
install actions; superseded descriptors remain in the hosted config for auditability and explicit replacement tracking,
not for normal user installation.

Use `tools\build_spcc_response_repository_dataset.py` to produce the hosted staging layout. The builder copies each
reviewed response repository under `responses/<repository_id>/<version>/`, emits `spcc_response_repositories.json` with
immutable-versioned release metadata, writes `spcc_response_dataset_manifest.json`, and produces a dataset card with the
upload and verification commands. The generated config must still pass
`tools\verify_spcc_response_repository_config.py` against the exact hosted or local base URL before it can be enabled.

## Release Gate

A response repository is production-ready only when:

- all repository and asset review states match the intended deployment mode;
- required license, attribution, source, and transformation metadata is present;
- reviewed source manifests validate with `tools\validate_spcc_response_source_manifest.py --strict-production-review`;
- normalizer output validates with `tools\validate_spcc_response_repository.py`;
- runtime deployment validates with `--runtime-root`;
- production review validates with `--strict-production-review`;
- managed descriptor configs validate with `tools\verify_spcc_response_repository_config.py`;
- managed-cache install smoke validates through `CatalogDownloadService` before a hosted descriptor is enabled;
- packaged builds discover and checksum the same reviewed assets;
- user-private repositories remain separate and still show private/unreviewed diagnostics.

## Implementation

`SpccResponseRepository.cpp` (`src/AfterNightProcesses/processes/SpectrophotometricColorCalibration/`) reads the
repository, resolves a profile and a white reference, and marks a profile built from unapproved assets as unreviewed,
which readiness refuses. The curves reach the engine's fit as
`src/AfterNightEngine/include/AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricFit.h` states;
"Photometric and spectrophotometric colour calibration" in `docs/dev/guides/PRODUCT_ARCHITECTURE.md` describes the product's side.
