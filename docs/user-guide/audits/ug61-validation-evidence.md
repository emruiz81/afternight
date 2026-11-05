# UG-61 Validation Evidence

## Scope

- Phase: UG-61, Narrowband Imaging.
- Status: **Blocked**.
- Date: 2026-07-20.
- Reviewed revision: `6293350b33f13e7f1c75d6a2f09f3b4c8669dd36`.
- Evidence modes: source/focused tests (`S`), approved-data execution (`E`),
  and build/structural validation (`B`).
- Gate: representative mono/OSC paths, per-filter integration, matching,
  SHO/HOO/custom palettes, representative continuum handling, star paths,
  checkpoints, recovery, and export.
- External tools, catalogs, and network: none.
- Later phase state: UG-62 was not started.

The machine-readable record is
`docs/user-guide/audits/ug61-validation-evidence.json`. It contains all 48
sanitized selected-file identifiers and SHA-256 values, historical partial-run
binary/test/output hashes, settings, stage outcomes, bounded downstream
observations, exclusions, and blocked-remediation validation. No representative
physical narrowband OSC or matched broadband-continuum set was supplied, so the
mandatory every-branch mode-E gate is not satisfied.

## Authorization and privacy

The maintainer supplied the project-owned session for this phase and authorized
its use for validation. The output trees were separate from the source session.
Repository-retained evidence omits absolute paths, observer and site/location
fields, device serial identifiers, private full reports/logs/manifests, and
generated astronomical images. Those artifacts remain only in the approved
isolated output trees.

The sanitized input-manifest SHA-256 is
`11796bc6d41363a4632d9e30ef9c6ba3ade6abff8f1947727e593e7e26287621`.

## Source-first resolution

Before execution, the current source and process documentation established:

- Image Integration grouping, calibration, shared registration, per-filter
  stacking, artifact, history, report, and output contracts;
- the Channel Composition, Linear Match, Narrowband Palette Composer, NB Color
  Mapper, NBRGB Combination, Split CFA, StarNet, native Star Mask, and Starmask
  Recombination routes;
- the palette composer's direct Ha/OIII/SII and OSC synthesis behavior;
- NB Color Mapper's per-layer colorization and screen blend;
- NBRGB's rectangular-bandwidth continuum estimate and guarded channel merge;
- Linear Match's accepted-range least-squares/moment behavior; and
- processing-state and save/reopen behavior.

Product and committed test sources were clean at source-review time. The only
dirty file was the required phase-ledger transition to `In progress`; the
reusable test was then extended as an execution artifact.

The unresolved-GUI checklist contained no standalone layout, dynamic
visibility, or interaction claim. Current routes and defaults were covered by
source and focused programmatic UI tests, while mode E used the native workflow
core. The application was therefore not launched and no mode-G claim is made.

## Reusable execution harness and build

The opt-in GoogleTest cases live in the existing reusable source
`src/AfterNightTests/Test_ImageIntegration/integration/test_ug60_real_data.cpp`.
UG-61 reads only `AFTERNIGHT_UG61_DATA_ROOT` and
`AFTERNIGHT_UG61_OUTPUT_ROOT`, skips when either is absent, embeds no private
path, and changes no product behavior. It calls the maintained manifest,
executor, stages, engine algorithms, and codecs.

The original partial run incorrectly marked an engine Palette Composer result
linear before export. Production marks every Palette Composer process output
nonlinear. The corrected harness now uses the production nonlinear-state
contract and expects a nonlinear FITS state on reopen. Because the approved-data
environment variables are unavailable during remediation, that corrected
private-data path still requires a fresh authorized run.

Current remediation build results:

1. `build\scripts\msvc\09a-build_afternight_tests_release.bat` — PASS,
   zero warnings/errors.
2. `build\scripts\msvc\09b-build_afternight_tests_debug.bat` — PASS,
   zero warnings/errors.

From the deployed `bin\msvc\<configuration>` tree with both UG-61 environment
variables set and `QT_QPA_PLATFORM=offscreen`, the focused cases are:

```cmd
Test_ImageIntegration.exe --gtest_filter=UG61RealDataWorkflow.MonoIntegration --gtest_color=no
Test_ImageIntegration.exe --gtest_filter=UG61RealDataWorkflow.PalettesOscContinuumStarsAndExport --gtest_color=no
```

The Debug executable uses the same filters with its `_d` suffix. These commands
now exercise the corrected nonlinear export contract, but currently skip when
the two approved-data variables are absent.

## Selected inputs and integration settings

The sample `UG61-MONO-SHO-M16-2025-06-18` is a `3008 x 3004`, 1x1,
monochrome Ha/OIII/SII session. Deterministic evenly spaced selection produced:

| Group | Selected | Recorded fact |
| --- | --- | --- |
| Ha/OIII/SII lights | 8 per filter; 24 total | 300-second linear mono lights |
| Ha/OIII/SII flats | 8 per filter; 24 total | Same camera mode; per-filter grouping |

Every selected input has a sanitized ID and SHA-256 in the JSON. Eight inputs
per group make the validation bounded; they are not a production frame-count
recommendation.

Executed settings:

- Maximum parallel cores `8`; light calibration in memory; flat auto-level on;
  cosmetic correction off.
- Per-filter flats: Average, Sigma Clipping `3.0/3.0`, Global Additive With
  Scaling, IKSS, no weighting.
- One 24-light registration group: fixed Ha reference, native star matching,
  Homography, Auto transform behavior, Standard alignment, Auto interpolation;
  no solver, catalog, or Gaia enrichment.
- Three integration groups: Average, Winsorized Sigma Clipping `3.0/3.0`,
  Global Additive With Scaling, IKSS, no weighting/drizzle/final solve.

The session contains no approved bias or dark frames. Each master-flat group
therefore emitted the expected warning that its flat was uncalibrated. This is
a recorded fixture limitation, not acquisition guidance.

## Historical partial execution results

| Configuration/filter | Result | Bounded observation |
| --- | --- | --- |
| Release `MonoIntegration` | PASS 1/1, 21.676 s GoogleTest; 20.451 s workflow | Flat stacking, common registration/alignment, normalization, and three per-filter stacks completed. All 24 lights were effective and registered; Ha/OIII/SII FITS masters and HTML/Markdown reports were produced with zero errors and three expected flat warnings. |
| Release downstream | PASS 1/1, 1.483 s | Center crops completed background correction and Ha-reference matching; SHO, HOO, Foraxx, and custom mapper results were finite/nonblank; scoped OSC/continuum paths, native star split/recombine, export/readback, a negative palette path, and save retry passed. |
| Debug both filters | PASS 2/2, 797.123 s | The same gate passed; the unoptimized workflow portion was 777.882 s. This is configuration validation, not a general performance claim. |

The downstream run used centered `1024 x 1024` representative crops.
Background Extraction used 20 samples/line, tolerance `2.0`, RBF smoothing
`0.50`, subtraction; sample counts were Ha `323`, OIII `357`, and SII `371`.
Linear Match used Ha as reference with accepted range `1e-7..0.875`.

Historical palette checkpoints:

- direct SHO, HOO, and Foraxx engine results were finite, nonblank,
  three-channel 32-bit float images; the old harness incorrectly assigned them
  linear processing state even though the production process marks them
  nonlinear;
- custom NB Color Mapper used SII hue/saturation `18/0.82`, Ha `52/0.72`, and
  OIII `195/0.78`, produced three colorized layers, and a finite/nonblank
  screen-blended output;
- omitting OIII raised the expected process error; and
- the native star mask selected `0.744%` of the representative image, both
  split products were finite/nonblank, and additive recombination matched the
  stretched source within sampled mean absolute delta `1e-6`.

The historical `1024 x 1024 x 3` working master reopened as 32-bit float FITS
with the expected HISTORY, but its asserted linear processing state was produced
by the erroneous test-only marker and is invalidated. The corrected harness
expects nonlinear state and must be rerun. The historical share copy reopened as
16-bit PNG with finite/nonblank content. Saving to a missing parent failed, then
succeeded and reopened after creating the parent and retrying.

## OSC and continuum branch boundaries

No physical narrowband OSC or matched broadband-continuum session was supplied
for UG-61. The mandatory algorithm paths were therefore exercised with
explicitly labeled same-target derivatives from the validated mono masters:

- The OSC1 derivative placed Ha in red and OIII in green/blue. The palette
  composer synthesized Ha from red and OIII from mean green/blue; derived HOO
  matched direct HOO within sampled mean absolute delta `1e-6`.
- The continuum derivative fed the mapped HOO image and same-target Ha/OIII
  channels to NBRGB using RGB bandwidth `100 nm`, red `7/1.20`, green and blue
  `8.5/1.20`, fit rejection `0..0.92`, and nonlinear rescale off.

These prove native path reachability and arithmetic on representative target
signal. They do not prove raw-CFA calibration/demosaic, physical dual-band
separation, sensor/noise behavior, measured broadband continuum, filter
transmission accuracy, or photometric fidelity. The guide states those limits
and tells users to use real OSC/broadband inputs for those claims. Under the
UG-60--UG-64.x prerequisite, these proxies cannot substitute for representative
project-owned data, so UG-61 remains blocked.

## Focused regression coverage

The exact Release commands are:

```cmd
Test_ImageOperations.exe --gtest_filter="ImageColorCalibration.LinearMatch*:NBRGBCombination.*:NBColorMapper*:NarrowbandPaletteComposer*" --gtest_color=no
Test_ProcessFramework.exe --gtest_filter="ChannelCompositionProgrammaticUiTest.*:LinearMatchProgrammaticUiTest.*:NarrowbandPaletteComposerProcessContracts.*:NarrowbandPaletteComposerProgrammaticUiTest.*:NBColorMapperProcessContracts.*:NBColorMapperProgrammaticUiTest.*:StarmaskRecombinationProgrammaticUiTest.*" --gtest_color=no
```

Debug uses the same commands with `Test_ImageOperations_d.exe` and
`Test_ProcessFramework_d.exe`. The exact source files covered by these filters
are listed in the Narrowband Imaging page evidence and tracker row.

In the current remediation run, both Release and Debug reported:

- `Test_ImageOperations` passed 61/61 focused Linear Match, NB Color Mapper,
  NBRGB Combination, and Narrowband Palette Composer tests.
- `Test_ProcessFramework` passed 21/21 focused Channel Composition, Linear
  Match, Narrowband Palette Composer, NB Color Mapper, and Starmask
  Recombination contract/programmatic UI tests.
- The corrected `UG61RealDataWorkflow.*` filter compiled and skipped 2/2 in
  each configuration because the approved-data environment variables were
  absent. This validates the opt-in harness only; it does not satisfy mode E.

## Claims deliberately excluded

- Eight selected frames are enough for a production master.
- Uncalibrated flats are recommended.
- The derived OSC or continuum proxies validate physical acquisition branches.
- UG-61 satisfies the representative-data gate or is review-ready.
- StarNet, an external model, a catalog, a solver, or network readiness.
- Polished full-frame aesthetic or print acceptance.
- General runtime, peak-memory, disk-multiplier, Linux, installer,
  technical-review, or user-review acceptance.

## Terminal documentation gates

The synchronized terminal results are:

- Deterministic audit: 112 navigated/tracked pages, zero parity findings, three
  later pages with 39 required-heading findings, 74 recorded-source-drift
  pages, zero partial-source records, 77 canonical process tables, and SHA-256
  `1f611e191776f71deef036677ae6bbe3fbd5621a89ec29f567dbdd137a567674`.
- `validate_ug01_user_guide_audits.ps1` — PASS in 684.9 seconds: baseline hash
  and expected findings match; two reports are byte-identical; menu parity and
  committed/untracked/deleted/unavailable-source fixtures pass.
- `build\scripts\msvc\build_user_guide_docs.bat` — PASS at terminal
  synchronization.
- `build\scripts\msvc\run_tests.bat` with all UG-60/UG-61 private-data
  variables unset — PASS for every discovered Release `Test_*.exe` executable
  in 1,067.6 seconds.
- MSVC Release and Debug test builds — PASS from the formatted source with zero
  warnings/errors.
- Current Release and Debug focused filters — PASS 61/61 engine and 21/21
  process/UI tests per configuration; corrected private-data filters SKIP 2/2
  per configuration because the approved data is unavailable.
- `clang-format 20.1.8 --dry-run --Werror` on the complete changed test — PASS.
  The official MSVC helper also exits PASS but compares `origin/main` to `HEAD`
  and reports no worktree lines; the Linux/CI helper could not run because this
  Windows environment has no `bash`.
- UG-00 completion-state validation — PASS: 112 tracker rows, 61 completed
  phase rows, and UG-61 explicitly Blocked. JSON parsing, privacy/scope checks,
  `git diff --check`, and final status are recorded at handoff.
