# UG-60 Blocker Evidence

## Scope and starting state

- Phase: UG-60, Broadband Imaging workflow. UG-61 was not started.
- Reviewed repository revision:
  `25a88e64f4084b10a23913c15d912caf540038e0`.
- Date: 2026-07-20.
- The worktree was clean before the required UG-60 plan-ledger transition. No
  product, test, guide, prerequisite, or tracker source was dirty at phase
  start.
- Required evidence mode: execution (`E`) with representative project-owned
  data for every OSC, RGB, and LRGB branch. Source/tests cannot substitute.

## Phase-order and source-resolution audit

- `AGENTS.md`, the completion plan, the completion tracker, the frozen workflow
  template, and the prerequisite inventory were read before phase work.
- UG-00 through UG-59 have terminal `Agent complete` or `Complete` states.
- Only UG-60 was moved from `Not started` to `In progress`; no later phase was
  opened.
- The current source paths needed for a resumed source/test audit were resolved
  from the current tree, including:
  - reachability: `src/AfterNightProcesses/app/ProcessRegistryBuiltins.cpp` and
    `src/AfterNightControls/app/AfterNightAppUiBuilder.h`;
  - project workflow:
    `src/AfterNightProcesses/processes/ImageIntegration/ImageIntegrationProcess.cpp`,
    `src/AfterNightProcesses/processes/ImageIntegration/model/**`,
    `src/AfterNightProcesses/processes/ImageIntegration/workflow/**`,
    `src/AfterNightProcesses/processes/ImageIntegration/reporting/**`, and
    `src/AfterNightTests/Test_ImageIntegration/**`;
  - branch handoffs:
    `src/AfterNightProcesses/processes/DebayerProcess.cpp`,
    `src/AfterNightProcesses/processes/ChannelCompositionProcess.cpp`, and
    `src/AfterNightProcesses/processes/LRGBCombinationProcess.cpp`;
  - downstream processing:
    `src/AfterNightProcesses/processes/BackgroundExtractionProcess.cpp`,
    `src/AfterNightProcesses/processes/AutoGradientRemovalProcess.cpp`, the
    native color-calibration, noise-reduction, stretching, and star-process
    implementations under `src/AfterNightProcesses/processes/`, and their
    focused tests under `src/AfterNightTests/`;
  - save/export:
    `src/AfterNightControls/app/FileManager.cpp`,
    `src/AfterNightControls/app/FileWorkflowRunner.cpp`, and the engine's save
    options and codec tests.
- The existing Broadband Imaging links resolve to current guide files. The page
  remains a non-template-complete `Expansion required` page because the
  mandatory execution prerequisite failed before a verified rewrite could be
  completed.

## Read-only data availability audit

The repository contains documentation images, CFITSIO test files, and synthetic
test fixtures, but no approved OSC/RGB/LRGB sample inventory with provenance and
permission. Those assets cannot satisfy the workflow gate.

Read-only inspection outside the repository found possible private candidates:

| Candidate | Observed availability | Why it does not satisfy UG-60 |
| --- | --- | --- |
| Private luminance/RGB session tree A | A frame tree with Bias, Dark, Flat, Light, and Snapshot directories; filenames indicate Luminance, Red, Green, and Blue material | No project-ownership/permission record, no approved subset manifest, and no authorization to retain hashes, reports, observations, or derivative outputs as documentation evidence |
| Private M83 project tree | Raw sessions plus prior AfterNight manifests, reports, registration artifacts, and L/R/G/B stack outputs | Earlier documentation approval covered only one M83 luminance master for UG-16. It did not approve the complete source set or extend permission to OSC/RGB/LRGB workflow evidence for UG-60; the retained prerequisite record explicitly treats that luminance-only artifact as inapplicable to broadband RGB evidence |
| Private CFA test collection | Small Canon, Motorola, and Nikon RAW/CFA test collections | Not a representative calibrated OSC workflow set and no UG-60 provenance/permission record |
| Third-party QHY533M narrowband archive | Third-party H/O/S narrowband example files and masters | Not project-owned broadband data and not applicable to OSC/RGB/LRGB coverage |

No files were opened in AfterNight, changed, copied, hashed, or used to produce
derivatives. No application or external tool was launched.

## Unresolved GUI and execution checklist

There is no standalone mode-`G` claim to inspect before data approval. Every
remaining runtime observation belongs to mandatory mode `E` and must use an
approved, isolated output tree:

| Branch | Required approved input | Required executed checkpoints |
| --- | --- | --- |
| OSC | Representative raw CFA lights with compatible calibration frames and recorded CFA/metadata assumptions | Import/grouping; calibration coverage; raw-before-demosaic handoff; registration/alignment; integration report and linear color master; background/color/noise/stretch/finish handoffs; working-master and share export verification |
| RGB | Representative mono R/G/B lights with compatible per-filter calibration frames | Per-filter grouping/calibration/registration/integration; aligned channel-master acceptance; RGB composition; linear and nonlinear checkpoints; export verification |
| LRGB | Representative mono L/R/G/B lights with compatible per-filter calibration frames | Per-filter integration; RGB composition; luminance/chrominance compatibility and LRGB combination; linear/noise/stretch/finish checkpoints; export verification |

For each branch, the resumed run must retain the approved sample identifier,
provenance and permission, selected-file checksums or an approved manifest,
application revision/configuration, exact settings, logs/reports, output hashes,
observable acceptance decisions, failures/recovery exercised, and explicit
limits on what the run proves. If one sample set covers both RGB and LRGB, the
approval must explicitly authorize both branches and the evidence must still
record them separately.

## Exact blocker and resume condition

UG-60 is blocked because no maintainer-approved sample inventory currently maps
representative project-owned data to all OSC, RGB, and LRGB branches with
permission to use the inputs and retain derived documentation evidence. The
presence of private candidate files is not approval.

Resume UG-60 only after the maintainer records:

1. the approved path or manifest for every branch;
2. ownership/provenance and permission for documentation validation;
3. whether private paths/metadata must be redacted and which derived artifacts
   may be retained;
4. a safe isolated output location with enough disk space; and
5. any required catalog/tool choice and readiness, or an explicit native-only
   workflow that avoids optional external dependencies.

Until then, no current-build mode-`E` run can be performed, the primary page
cannot become a review-ready workflow Draft, and the phase cannot be marked
`Agent complete` or `Complete`.

## Validation

- `powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\validate_ug00_user_guide_foundation.ps1`
  — PASS: 112 tracker rows with 17 fields, 60 completed phase-ledger rows,
  four templates, 32 prerequisite groups, and clean foundation whitespace.
- `build\scripts\msvc\build_user_guide_docs.bat` — PASS: strict MkDocs build
  completed in 6.89 seconds.
- `git diff --check` — PASS before this final evidence synchronization and must
  pass again in the terminal diff audit.
- No C/C++ file changed, so application/test builds, deploy, and changed-line
  clang-format are not applicable. No focused runtime test can replace the
  missing mode-`E` evidence.
- The full UG-01 validator was not run because its UG-59 regression fixture
  intentionally asserts that UG-60 is still `Not started`. Advancing that
  historical validator in a blocked content phase would be out-of-scope audit
  framework work under section 7.2. The navigated Broadband page and checked
  audit baseline were not changed.

## Resolution and resumed scope

The maintainer resolved the blocker on 2026-07-20 and authorized the resumed
mode-`E` phase with these sanitized records:

| Requirement | Resolution |
| --- | --- |
| OSC branch | Project-owned identifier `UG60-OSC-NGC253-QHY183C-2023-10-09`; header-only inspection found 35 RGGB lights plus bias, dark, flat, and dark-flat calibration sets |
| RGB and LRGB branches | Project-owned identifier `UG60-RGB-LRGB-M83-QHY533M-2025-04-29`; header-only inspection found mono L, R, G, and B lights plus per-filter flats |
| Rights and retention | The data owner approved AfterNight documentation validation and retention of sanitized settings, reports, logs, checksums, derived-output observations, and generated derivatives |
| Privacy | Retained evidence omits absolute input/output paths, observer and site/location fields, and device serial identifiers |
| Output | A new isolated validation output tree on the data volume was approved; its absolute path remains private |
| Tools and catalogs | Native-only AfterNight workflow; no optional external catalog, response pack, solver, command-line tool, or model |

This section records only prerequisite resolution. It does not claim that the
workflow run, outputs, page rewrite, or final UG-60 validation has passed.

## Final resumed outcome

The resumed work subsequently satisfied the recorded mode-E gate and moved
UG-60 to `Agent complete` on 2026-07-20. A deterministic subset selected and
hashed 104 approved frames across OSC and mono L/R/G/B source/calibration
groups. Current native Windows MSVC Release filters pass 1/1 for OSC, 1/1 for
mono RGB/LRGB, and 1/1 for reusable downstream background/color/denoise/star/
export coverage. Expected reports and OSC/L/R/G/B/RGB/LRGB products were
written; working FITS files reopened as nonblank 32-bit float with expected
state/history, and delivery PNG files reopened as nonblank unsigned 16-bit at
the recorded geometry/channel counts. The downstream filter also verifies a
missing-parent save failure and successful create-parent/retry/readback. The
four mono missing-bias/dark flat warnings are retained as limitations rather
than suppressed.

The review-ready Broadband page and exact sanitized settings, checksums,
results, privacy boundaries, and exclusions are retained in:

- `docs/user-guide/docs/en/workflows/broadband-imaging.md`;
- `docs/user-guide/audits/ug60-validation-evidence.md`; and
- `docs/user-guide/audits/ug60-validation-evidence.json`.

This final outcome does not retroactively alter the historical blocker record
above. It records the separate resumed execution after the maintainer supplied
the missing authority and output location. Media remains assigned to UG-73;
technical and user review remain UG-78/UG-80. UG-61 was not started.
