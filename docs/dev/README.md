# Developer documentation

What is in `docs/dev/`. The rules are [AGENTS.md](../../AGENTS.md) at the repository root, and
[CLAUDE.md](../../CLAUDE.md) is its quick operational summary; [BUILD.md](../../BUILD.md) walks through a build from
source and [CONTRIBUTING.md](../../CONTRIBUTING.md) through a contribution.

Everything here is current guidance - it describes the code as it is, and a change to the code updates it in the
same change - or the input of a tool. A design's reasons are written in the guide for its area, beside how it
works; there are no decision records.

## Current guidance

### Architecture and contracts

- [guides/](guides/README.md) - the engine's guides (architecture, the numeric and validity model,
  backends, dependencies, build, testing, benchmarks) and
  [the product on the engine](guides/PRODUCT_ARCHITECTURE.md), area by area: how each part works and why it is
  built the way it is.
- [PUBLIC_API_CONTRACT.md](PUBLIC_API_CONTRACT.md) - what is true of every published header of the engine.
- [EXTENSION_HOST_MODES_AND_LICENSING.md](EXTENSION_HOST_MODES_AND_LICENSING.md) - the full and Lite extension
  hosts, the runtime backend's contract and the licensing boundary between them.
- [PLUGIN_SDK_SUPPORTED_DOMAIN_MATRIX.md](PLUGIN_SDK_SUPPORTED_DOMAIN_MATRIX.md) - which SDK domains each backend
  publishes.
- [THEME_SYSTEM.md](THEME_SYSTEM.md) - the UI theme: design tokens, the proxy styles and the rules every widget
  follows.

### The ImageIntegration workflow

- [IMAGE_INTEGRATION_ARTIFACT_VALIDATION.md](IMAGE_INTEGRATION_ARTIFACT_VALIDATION.md) - how the workflow writes its
  artifacts and decides to reuse them.
- [IMAGE_INTEGRATION_REPORT.md](IMAGE_INTEGRATION_REPORT.md) - how a run's report is built: when it is written,
  what its archive keeps of it, what it reads, the model it is stated in, how its values are printed, how its charts
  are drawn and its document written, how it is written again and saved as one file, how to open sample reports in
  real browsers, and the runs its suites build.
- [NO_DATA_SAMPLE_CONTRACT.md](NO_DATA_SAMPLE_CONTRACT.md) - how exact-zero and non-finite samples are marked and
  handled.
- [OVERLAP_NORMALIZATION.md](OVERLAP_NORMALIZATION.md) - overlap-based global normalization.
- [FRAME_WEIGHTING_INPUTS.md](FRAME_WEIGHTING_INPUTS.md) - what each stacking weight reads and where it is kept.

### Building, validating and debugging

- [LOCAL_VALIDATION.md](LOCAL_VALIDATION.md) - how a change is validated on Windows and Linux: the
  common check list, each platform's commands and the campaigns.
- [INCREMENTAL_BUILD_DEPENDENCY_TRACKING.md](INCREMENTAL_BUILD_DEPENDENCY_TRACKING.md) - the MSVC stale-object
  hazard and its two guards.
- [CRASH_SYMBOLICATION.md](CRASH_SYMBOLICATION.md) - where crash dumps go and how to symbolicate them.
- [XISF_PIXINSIGHT_CHECK.md](XISF_PIXINSIGHT_CHECK.md) - one unit of each form the XISF writer produces, read in
  PixInsight: how to run the check and what the last one found.

### Catalogs and spectrophotometric colour calibration

- [CATALOG_DISTRIBUTION.md](CATALOG_DISTRIBUTION.md) and [GENERATING_GAIA_CATALOGS.md](GENERATING_GAIA_CATALOGS.md) -
  the Gaia catalog packs: generating, uploading and serving them.
- [SPCC_CATALOG_REQUIREMENTS.md](SPCC_CATALOG_REQUIREMENTS.md),
  [SPCC_CATALOG_DISTRIBUTION.md](SPCC_CATALOG_DISTRIBUTION.md),
  [SPCC_CATALOG_MAINTENANCE.md](SPCC_CATALOG_MAINTENANCE.md),
  [SPCC_RESPONSE_PROVENANCE.md](SPCC_RESPONSE_PROVENANCE.md),
  [SPCC_USER_WORKFLOW.md](SPCC_USER_WORKFLOW.md) and
  [SPCC_MONO_NARROWBAND_NOTES.md](SPCC_MONO_NARROWBAND_NOTES.md) - the SPCC catalog and response datasets, their
  maintenance, the workflow and its support boundary.
- [spcc/spcc-production-release-evidence-checklist.md](spcc/spcc-production-release-evidence-checklist.md) and the
  templates and manifests beside it in [spcc/](spcc/) - the SPCC production release gate, which is still open.

### Provenance of derived code

[AUTO_GRADIENT_REMOVAL_PROVENANCE.md](AUTO_GRADIENT_REMOVAL_PROVENANCE.md),
[NBCOLORMAPPER_PROVENANCE.md](NBCOLORMAPPER_PROVENANCE.md),
[NARROWBAND_PALETTE_COMPOSER_PROVENANCE.md](NARROWBAND_PALETTE_COMPOSER_PROVENANCE.md),
[NBRGB_COMBINATION_PROVENANCE.md](NBRGB_COMBINATION_PROVENANCE.md),
[DEMOSAIC_PROVENANCE.md](DEMOSAIC_PROVENANCE.md),
[PATCH_DENOISE_PROVENANCE.md](PATCH_DENOISE_PROVENANCE.md),
[MEDIAN_NETWORKS_PROVENANCE.md](MEDIAN_NETWORKS_PROVENANCE.md),
[DRIZZLE_CORNER_INTERPOLATION_PROVENANCE.md](DRIZZLE_CORNER_INTERPOLATION_PROVENANCE.md),
[STAR_DETECTION_PROVENANCE.md](STAR_DETECTION_PROVENANCE.md),
[BACKGROUND_SAMPLING_PROVENANCE.md](BACKGROUND_SAMPLING_PROVENANCE.md),
[DEEPSKYSTACKER_PROVENANCE.md](DEEPSKYSTACKER_PROVENANCE.md),
[WAITING_SPINNER_PROVENANCE.md](WAITING_SPINNER_PROVENANCE.md) and the ported records that
[guides/DEPENDENCIES.md](guides/DEPENDENCIES.md) lists under "Ported code": where each derived piece of code came
from, under which licence, and which files carry its notice ("Credit for derived code" in AGENTS.md).

### The user guide

- [USER_GUIDE_AUTHORING.md](USER_GUIDE_AUTHORING.md) - how the user guide is built and written: its theme, hooks,
  data and packaging, the page metadata, the content rules, what makes a page complete, and the checks that hold
  it.
- [USER_GUIDE_STYLE.md](USER_GUIDE_STYLE.md) - how a page of the user guide is written: who reads which kind of
  page, voice and sentences, what a page does not say, pictures, troubleshooting, credits and length.
- The page templates under [../user-guide/templates/](../user-guide/templates/), one per kind of page, and the
  budgets, the vocabulary and the inventory of the process windows' controls under
  [../user-guide/data/](../user-guide/data/) that `check_user_guide.py` holds every page to.
- [../user-guide/SCREENSHOTS.md](../user-guide/SCREENSHOTS.md) - how a picture of a window is captured from the
  application and prepared for a page.

## Inputs the tools read

These live here but are data: a tool reads each one, and changing it is a change to what that tool holds.

- `budgets.json` - the include and compile-time budgets (`check_budgets.py`).
- `raw-codec-corpus.json`, `rawspeed-corpus.json`, `rawspeed-goldens.json` and
  `rawspeed-public-samples.json` - the RAW codec corpus and its goldens (`run_raw_codec_corpus.py`,
  `rawspeed/qualify.py`).
- `dependency-lock.json` - the dependency inputs a build is made from - the submodule commits, the Qt version and
  the embedded-Python artifacts - whose digest every benchmark run records ([guides/BENCHMARKS.md](guides/BENCHMARKS.md)).
