# UG-60 Validation Evidence

## Scope

- Phase: UG-60, Broadband Imaging.
- Date: 2026-07-20.
- Reviewed revision: `ec8d153349d6c7adb3107fa4d81fcddffc3f448c`.
- Evidence modes: source/focused tests (`S`), approved-data execution (`E`),
  and build/structural validation (`B`).
- Branches: OSC, mono RGB, and mono LRGB.
- External tools/catalogs/network: none.
- Later phase state: UG-61 was not started.

The complete machine-readable record is
`docs/user-guide/audits/ug60-validation-evidence.json`. It contains all 104
sanitized selected-file identifiers and SHA-256 values, build and reusable-test
hashes, stage results, warnings, product/report hashes, representative
downstream observations, and strict export/readback results.

## Authorization and privacy

The maintainer approved two project-owned sets for AfterNight documentation
validation and approved retention of sanitized settings, reports, logs,
checksums, derived-output observations, and generated derivatives. The approved
output tree was isolated from both sources.

Repository-retained evidence omits:

- absolute input and output paths;
- observer and site/location fields;
- device serial identifiers; and
- private full logs, reports, manifests, and generated images.

Those private execution artifacts remain only in the approved output tree. The
sanitized input-manifest SHA-256 is
`177623479a0f0e356aba648c322d88ec9785743e965b8feb1d3c020629149970`.

## Source-first resolution

The source/test audit was completed before execution. It established the
current Image Integration reachability, model, manifest, calibration matching,
stage graph, settings, artifacts, reporting, RGB combination, LRGB handoff,
processing-state, and save/export contracts. Product and committed test sources
were clean at audit time.

The unresolved-GUI checklist contained no standalone rendering, dynamic
visibility, or interaction claim. The mandatory execution evidence could be
produced deterministically through the current native workflow core, so no
exploratory application session or mode-G claim was made.

## Build and reusable execution harness

Commands and results:

1. `build\scripts\msvc\05a-build_afternight_release.bat` — PASS, zero
   warnings/errors.
2. `build\scripts\msvc\check_clang_format.bat origin/main` plus direct
   `clang-format --dry-run --Werror` on the added test — PASS.
3. `build\scripts\msvc\01-create_project_files.bat` — PASS after the reusable
   source was added.
4. `build\scripts\msvc\09a-build_afternight_tests_release.bat` and
   `09b-build_afternight_tests_debug.bat` — PASS, zero warnings/errors.

The opt-in GoogleTest at
`src/AfterNightTests/Test_ImageIntegration/integration/test_ug60_real_data.cpp`
reads its private manifest only from `AFTERNIGHT_UG60_MANIFEST`, skips when that
variable is absent, contains no approved path, and changes no product behavior.
It calls the same `WorkflowManifest`, `WorkflowExecutor`, native stages, reports,
image operations, and codecs used by the maintained product. Its SHA-256 and
size are retained in the JSON evidence.

From `bin\msvc\<configuration>`, with
`AFTERNIGHT_UG60_MANIFEST=<approved-private-manifest>` and
`QT_QPA_PLATFORM=offscreen`, run:

```cmd
Test_ImageIntegration.exe --gtest_filter=UG60RealDataWorkflow.Osc --gtest_brief=1 --gtest_color=no
Test_ImageIntegration.exe --gtest_filter=UG60RealDataWorkflow.MonoRgbLrgb --gtest_brief=1 --gtest_color=no
Test_ImageIntegration.exe --gtest_filter=UG60RealDataWorkflow.DownstreamHandoffsAndExport --gtest_brief=1 --gtest_color=no
```

## Selected inputs

| Branch/group | Selected | Recorded acquisition facts |
| --- | --- | --- |
| OSC bias, dark, dark-flat, flat, light | 8 each; 40 total | QHY183C, `5544 x 3684`, RGGB, 1x1, gain 11, offset 30; lights/darks 180 s; flats/dark-flats 0.39 s |
| Mono L/R/G/B lights | 8 each; 32 total | QHY533M, `3008 x 3004`, mono, 1x1, gain 76, offset 35; L 60 s; RGB 90 s |
| Mono L/R/G/B flats | 8 each; 32 total | Same camera mode and per-filter grouping; no approved mono bias/dark frames were present |

Every selected file has a sanitized ID and full SHA-256 in the JSON record.
Selection was deterministic across each approved group; it was not a claim that
eight inputs are a production-optimal stack size.

## Executed settings

- Maximum parallel cores: 8.
- Calibration masters: Average, Sigma Clipping `3.0/3.0`, no weighting; flat
  normalization Global, other calibration normalization None.
- Light calibration: in memory, flat auto-level on, cosmetic correction off.
  OSC used matched dark + dark-flat-calibrated flat; mono used per-filter flat
  only.
- Registration: native star matching, fixed reference, Homography with Auto
  transform behavior, standard alignment, Auto interpolation; OSC RGGB with RCD
  demosaic; no auto solver or Gaia enrichment.
- Light stacking: Average, Winsorized Sigma Clipping `3.0/3.0`, Global Additive
  Scaling, IKSS, no weighting, no drizzle/maps/final solve.
- Mono workflow: one shared L-reference registration group, four filter
  integration groups, and workflow RGB combination enabled.
- LRGB downstream: default Lightness/R/G/B weights `1.0`; Lightness/Saturation
  MTF `0.5`.
- Representative downstream crops: centered `1024 x 1024`; Background
  Extraction `20` samples/line, tolerance `2.0`, RBF, smoothing `0.50`,
  subtraction; OSC/RGB automatic Color Calibration with centered `512 x 512`
  white and upper-left `256 x 256` background references; Wavelet Denoise
  threshold `1.2`, scales `4`, iterations `10`, Amount `0.65`.
- Finish: MAD autostretch, shadows `-2.8`, target background `0.25`, RGB
  unlinked; StarProfilerV1 detection `0.45`, roundness `2.0`, Truncated Bell,
  size `2..25`, Percent `0.50`; masked Circle-3 erosion, one iteration, Amount
  `0.20`.

## Results

| Filter | Result | Bounded observation |
| --- | --- | --- |
| `UG60RealDataWorkflow.Osc` | PASS 1/1, 42.061 s GoogleTest elapsed | Bias, dark, flat, registration, alignment, normalization, and stacking completed. One `5544 x 3684` RGB FITS master and HTML/Markdown report were produced; zero warnings/errors. |
| `UG60RealDataWorkflow.MonoRgbLrgb` | PASS 1/1, 27.448 s GoogleTest elapsed | Flat, registration, alignment, normalization, and stacking completed. L/R/G/B and combined `3008 x 3004` RGB FITS masters and reports were produced. Four warnings accurately reported uncalibrated flats because no mono bias/dark was approved. |
| `UG60RealDataWorkflow.DownstreamHandoffsAndExport` | PASS 1/1, 24.957 s GoogleTest elapsed | OSC/RGB/luminance crops completed the native background, color where applicable, denoise, stretch, star-mask, and masked-morphology checkpoints. FITS and PNG outputs passed finite/nonblank, precision, geometry/channel, state/metadata-as-applicable readback. A missing-parent save failed, then passed after creating the parent and retrying. |

The terminal Release repeat passed all three filters together, 3/3 in 55.792
seconds. Debug passed the expanded downstream/export filter, 1/1 in 84.713
seconds. A combined Debug all-three run exceeded the 240-second command window
before producing a summary, so it is not recorded as either pass or failure.
With the private manifest unset, the complete Release `Test_ImageIntegration`
executable passed 2,128 tests and skipped only the three private-data cases in
266.088 seconds.

Generated OSC and RGB report previews were inspected only for blank/corrupt
output. Both were recognizable astronomical fields with the intended targets.
The RGB preview also showed strong channel/background mismatch and stacking
borders. This is retained as a checkpoint/recovery observation, not an
aesthetic endorsement.

The representative downstream checkpoints were also inspected. Diagnostic
background models showed smooth low-frequency structure without recognizable
target detail. The initial Star Mask default (`0.10`) selected widespread noise;
`0.45` produced sparse masks confined to bright stars. Before/after masked
morphology showed restrained star changes without obvious dark rings or target
damage. The OSC white-reference mask followed substantial target structure, so
it proves that the diagnostic mask exposes a questionable reference, not that
the reference is photometrically valid. Exact mask coverage and stage deltas
are in the JSON record.

Wavelet Denoise cycle-spinning can produce small run-to-run changes in numeric
deltas and derived masks. The reusable test therefore asserts finite/nonblank
content, bounded nonempty/non-full masks, nonzero stage deltas, types, state,
history, and readback rather than byte-identical downstream derivatives.

## Claims deliberately excluded

- Full approved-set production output or a recommendation to use eight frames.
- Full-frame gradient/color/noise/star settings, photometric color accuracy,
  creative finishing, polished-image quality, or print acceptance.
- General runtime, peak memory, disk multiplier, or hardware performance.
- External model/tool/catalog/solver/network readiness.
- Linux, installer, print, interoperability, technical-review, or user-
  review acceptance.

The page therefore gives source-backed concrete starting points and checkpoint
criteria, while the worked-run section limits execution claims to the exact
recorded artifacts.

## Terminal documentation gates

The final UG-60 remediation retained the reusable test, regenerated the project,
and rebuilt both test configurations. Terminal documentation results:

- `validate_ug00_user_guide_foundation.ps1` — PASS: 112 tracker rows with 17
  fields, 61 completed phase-ledger rows, four templates, 32 prerequisite
  groups, and clean foundation whitespace.
- `validate_ug01_user_guide_audits.ps1` — PASS in 684.4 seconds: baseline hash
  and expected findings match, two reports are byte-identical, and Image-menu
  parity plus committed/untracked/deleted-source drift fixtures pass.
- Deterministic audit — 112 navigated/tracked pages, zero parity findings, zero
  partial-source records, four later pages with 52 required-heading findings,
  73 recorded-source drift pages, 77 canonical process tables, and SHA-256
  `54587df6febc8731b01531ce5a745d41200b23381332d1adc383674f1f2ecda0`.
- `build\scripts\msvc\build_user_guide_docs.bat` — PASS after the substantive
  page rewrite in 7.08 seconds and after final metadata synchronization in 5.58
  seconds.
- Final JSON parsing, privacy scan, scope check, line-ending check, and
  `git diff --check` are recorded at handoff.
