# UG-65 Diagram Validation Evidence

Date: 2026-07-21  
Phase status: Agent complete  
Reviewed repository revision: `15a47bc468699395e8dc20d7cc57d3945775a7ae`  
Evidence modes: S (source/test review) and B (offline built-site browser inspection)

## Scope and starting state

The worktree was clean before the required UG-65 plan transition. No product or
test source was dirty at phase start, and UG-65 changes no C/C++ source. The
phase audited the 2026-07-18 baseline of 11 inline Mermaid diagrams plus one
semantic HTML/CSS flow. It retains eight Mermaid diagrams plus the HTML flow and
removes the Narrowband, Planetary, and Mosaic diagrams because their workflow
claims are maintainer-approved Deferred in UG-61 through UG-64.

Source and tests answered every product-behavior question before browser
inspection. The unresolved-GUI checklist was empty, so AfterNight was not
launched. No screenshot was captured; diagram media is committed source rather
than screenshot media, and later human reviews remain UG-78 and UG-80.

## Baseline gap and remediation

The pre-phase built site preserved Mermaid source as `<pre class="mermaid">`.
Material's generated runtime then attempted to load Mermaid from `unpkg.com`;
with HTTP(S) blocked in a `file://` session, the diagrams did not render. UG-65
therefore vendors Mermaid 11.16.0 and its MIT license, uses the project-specific
`an-mermaid` fence class, and renders explicit nodes from a local initializer
with Mermaid `securityLevel: "strict"`. This avoids Material's external fallback
and keeps installed/offline guide rendering deterministic.

The audit and review remediation corrected two substantive sequence errors:

- Image Integration Lite now branches after registration. Non-drizzle groups
  materialize aligned lights; drizzle groups retain transforms and resample
  during stacking.
- Full Image Integration now shows the same executor-controlled branch, with
  optional normalization before standard or drizzle stacking.

The three deferred pages retain their established audit headings and state why
no release diagram is present. The focused validator proves that their built
HTML contains no Mermaid host.

## Retained diagram inventory and source map

Hashes are SHA-256 over LF-normalized source: the complete Mermaid fence plus
its trailing newline, or the complete Home `an-flow-diagram` block plus its
trailing blank line. The same hashes are recorded in `assets/MEDIA_PROVENANCE.md`.

| Page and diagram | SHA-256 | Primary source/test evidence |
| --- | --- | --- |
| `index.md#typical-deep-sky-flow` — semantic Home flow | `8c037c820c8f7ff245884c9a31992a8da0dabf6e0ed3e2c42f6808aabc93ba8c` | Current Process Catalog and completed guide pages; `src/AfterNightProcesses/app/ProcessRegistryBuiltins.cpp` |
| `getting-started/quick-start.md#behavior-and-state-model` | `20771cf98285c2ddaf70ae7c18cc907c15c6d28c443abccf806e38da5d3ff0ac` | `ImageIntegrationLiteProcess.*`, `ImageIntegration/lite/LiteIntegrationProfile.*`, `core/FileImport.*`, `model/CalibrationMatcher.*`, `workflow/ManifestBuilder.*`, `WorkflowExecutionController.*`, `WorkflowProgressController.*`; Lite and workflow tests |
| `getting-started/supported-formats.md#behavior-and-state-model` | `956670363d73d06e3fe26435ef763bff492030f44693e0278b709e4d911882d4` | `FileManager.*`, `FileWorkflowRunner.*`, `ExportSequenceControl.*`, `src/AfterNightEngine/an_image_codecs.*`, codec/file-management tests |
| `interface/overview.md#behavior-and-state-model` | `32fa46a4090bae16f5beaee808c9e7e5cc28e4b4100683924b24986514cbdbcc` | `AfterNightApp.cpp`, `WindowLayoutCoordinator.*`, `ViewCreationCoordinator.*`, `FileManager.*`, dashboard/application tests |
| `processes/index.md#processing-map` | `650422843a3cfb6f51d18b7149e117879a148a275d12b7e6bdf3b6255fb35863` | `ProcessRegistryBuiltins.cpp`, `ProcessLauncher.*`, `ProcessRegistry.*`, process registry/menu tests, and `ug55-reviewed-destinations.json` |
| `processes/stacking-integration/image-integration-lite.md#how-the-algorithm-works` | `7349d7883c6615d1352edbb8ab0203b6c38a728b368c8c5c20f918565a918de6` | Lite process/profile plus Image Integration model, manifest, controller, executor, stage, and report sources/tests |
| `processes/stacking-integration/image-list-analyzer.md#how-the-algorithm-works` | `2308eaa8d3e10dbbbf06a4ef0252e3a56b2353dbadd7bc647a07659a80e0f330` | `ImageListAnalyzerProcess.*`, `ImageListProcessHelpers.h`, `ExportSequenceControl.*`, analyzer and codec tests |
| `processes/stacking-integration/image-integration.md#conceptual-pipeline` | `e11556c105b1383c046867223eda660c0f5ea55eb2706caf9435950805fab9b2` | `ManifestBuilder.*`, `WorkflowExecutor.*`, `WorkflowProgressController.*`, `workflow/stages/**`, `WorkflowReportBuilder.*`, focused Image Integration tests |
| `workflows/broadband-imaging.md#workflow-map-and-decision-points` | `5465b05cc9d1ddc623b21bd89a95e12a4b3a39906a71d895c15001356ab1306d` | Current integration/color/stretch/export sources and `Test_ImageIntegration/integration/test_ug60_real_data.cpp`; retained UG-60 evidence |

The detailed page sources and test paths remain in each page's authoring-only
Page evidence and tracker records. No product test was executed because UG-65
changes presentation and documentation claims only; source and already-recorded
focused test evidence were reviewed.

## Local runtime provenance

| Asset | Version/source | SHA-256 |
| --- | --- | --- |
| `assets/javascripts/mermaid/mermaid.min.js` | `mermaid@11.16.0`, npm tarball integrity `sha512-Zvm3kbstgdpvIJPPItlL7fppIZ3kibvc1oZIGxdvk9t6UFz6flv+Jw7FtRGKwfcI8OckmH04LqG6LlS6X4B1pA==` | `74d7c46dabca328c2294733910a8aa1ed0c37451776e8d5295da38a2b758fb9b` |
| `assets/javascripts/mermaid/LICENSE-MIT` | Mermaid 11.16.0 package license | `ec9fb67dcb25eccc416ed56e1aab819222c805a2a4bfe4cb19e7556bf2ffde80` |
| `assets/javascripts/mermaid/mermaid-init.js` | AfterNight local initializer | `fe3485ec6693be1f50de5087af3c79e2473c4118a952e8637f5797f7faac636d` |

## Browser matrix

`build/scripts/validate_ug65_user_guide_diagrams.cjs` uses
`playwright-core@1.61.1` with installed Microsoft Edge
`150.0.4078.83`. It blocks every HTTP(S) request and checks all nine diagrams at
360×800, 390×844, 768×1024, and 1280×900 in light and dark modes: 72/72 cases
PASS. It verifies the exact built inventory, deferred-page exclusion, local
assets, rendered SVG role/title/description, semantic Home lists, exact theme
colors, computed WCAG contrast thresholds, minimum effective text size,
horizontal overflow, and HTML/SVG content bounds.

| Diagram | Minimum effective text | Maximum horizontal overflow | 360 px light host (and SVG where applicable) |
| --- | ---: | ---: | --- |
| Home | 13.60 px | 0 px | 328×1011.8 |
| Quick Start | 12.49 px | 0 px | 328×572.8; SVG 322×544.8 |
| Supported Formats | 13.15 px | 0 px | 328×481.9; SVG 322×453.9 |
| Interface Overview | 16.00 px | 0 px | 328×310; SVG 313×282 |
| Process Catalog | 16.00 px | 0 px | 328×840; SVG 276×812 |
| Image Integration Lite | 12.48 px | 0 px | 328×1058.3; SVG 322×1030.3 |
| Image List Analyzer | 13.61 px | 0 px | 328×477.3; SVG 322×449.3 |
| Image Integration | 12.33 px | 0 px | 328×1064.3; SVG 322×1036.3 |
| Broadband | 16.00 px | 0 px | 328×1031.7; SVG 249.8×1003.7 |

The validator now computes and enforces contrast rather than only comparing
color strings. Mermaid minima are 14.37:1 label/node, 6.12:1 edge/surface, and
4.70:1 border/node across both themes; the semantic Home flow's minimum text
contrast is 6.55:1. All 72 cases report zero clipped HTML/SVG content. The
browser blocked three pre-existing MathJax NewCM font requests
from jsDelivr, but blocked external Mermaid requests were zero and every diagram
rendered. Total-site MathJax font localization belongs to the later offline
package/link audit rather than UG-65 and does not weaken the offline diagram
result.

## Validation commands and exact results

- Temporary validator setup (outside the repository):
  `$validatorRoot = Join-Path $env:TEMP 'afternight-ug65-playwright'; npm install --prefix $validatorRoot --no-save playwright-core@1.61.1`.
  The temporary package, screenshots, and generated audit report were removed
  after validation.
- `node build\scripts\validate_ug65_user_guide_diagrams.cjs` with
  `NODE_PATH=%TEMP%\afternight-ug65-playwright\node_modules`: PASS, 72 runs,
  zero failures, zero external Mermaid requests, zero horizontal overflow.
- `powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\audit_user_guide.ps1 -OutputPath %TEMP%\afternight-ug65-audit.md`:
  PASS in 408.5 seconds; 112 navigated/tracked pages, zero parity findings,
  three pages/39 required-heading findings, 71 recorded-drift pages, zero
  partial-source or boilerplate findings, and 77 canonical process tables;
  canonical SHA-256 `0f3bc6041668f84be7a3da771bae6e8fd85e626a85bf1ac2a44c149f7460d1e1`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\validate_ug01_user_guide_audits.ps1`:
  PASS in 790.4 seconds; baseline/expected findings, two byte-identical reports,
  Image-menu parity, committed/untracked/deleted/unavailable-source fixtures,
  and the new byte-identical pre-commit/post-commit drift regression all pass.
- `build\scripts\msvc\build_user_guide_docs.bat`: PASS; the final evidence-sync
  strict MkDocs build completed in 8.15 seconds, and the post-build check found the vendored Mermaid
  license in the offline site.
- `powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\validate_ug00_user_guide_foundation.ps1`:
  PASS; 112 tracker rows/17 fields, 62 completed phase-ledger rows, four
  deferred rows, four templates, 32 prerequisite groups, and clean foundation
  whitespace.
- Final focused browser rerun: PASS in 20.5 seconds, 72/72 runs, zero failures,
  zero overflow, zero clipped-content cases, contrast thresholds satisfied, and
  zero external Mermaid requests.
- `git diff --check`: PASS.

No C/C++ file changed, so product rebuild/deploy, unit-test execution, and
changed-line clang-format validation are not applicable.
