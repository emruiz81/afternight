# UG-59 Validation Evidence

## Scope and starting state

- Phase: UG-59, Troubleshooting And Diagnostics plus the Reference-section
  consistency review. UG-60 was not started.
- Reviewed repository revision:
  `fde465acf14253a45969baaf62dd8f629c703333`.
- Date: 2026-07-20.
- The worktree was clean before the required UG-59 plan-ledger transition. No
  reviewed product, test, packaging, workflow, or external-reference source was
  dirty at phase start.
- Evidence modes: source (`S`), primary upstream authority (`A`), focused
  deployed tests, and deterministic documentation/audit checks (`B`). No
  application, installer, AppImage, process, dataset, service, external tool,
  crash, or package-delivery path was executed.

## Source and cross-reference review

- Added one navigated/tracked Reference page with 54 canonical symptom rows and
  eight diagnostic-support rows. Every symptom links to a specific completed
  remedy page or section.
- Reconciled Keyboard Shortcuts, Glossary, File Formats, and System Requirements
  sequentially. The shortcut inventory remains 83 active commands. File-format
  and requirement matrices remain unchanged because no applicable product or
  test source changed after UG-57/UG-58.
- Expanded the Glossary from 187 to 193 terms with source-backed definitions for
  AVX2, crash dump, file log, keyboard focus, processing master, and System
  Information.
- Rechecked the current Qt 6.10 supported-platform boundary, Qt shortcut/action
  behavior family, AppImage FUSE/quickstart guidance, FITS 4.0 authority, and
  FFmpeg format authority on their existing reference pages. External facts are
  attributed to those primary sources rather than inferred from AfterNight.
- The unresolved-GUI checklist is live machine values, clipboard and file-dialog
  integration, actual crash capture, rendered layout, desktop key interception,
  non-US physical-key placement, and cross-platform runtime delivery. The pages
  make no claim that requires those observations, so no GUI launch was needed.

## Focused deployed tests

Working directory for every command: `C:\Development\afternight`.

Environment: deployed Windows MSVC Release and Debug (`_d.exe`) test executables
from `bin/msvc/release` and `bin/msvc/debug`; no environment override. Review
remediation reran every selected filter in both configurations after confirming
that the three Debug executables were present. No C++ behavior changed; prior
Release/Debug evidence remains recorded on the unchanged UG-57/UG-58 source
contracts.

### About dialog and Session Console bridge

```powershell
bin\msvc\release\Test_AfterNightApp.exe --gtest_filter="AboutDialogTest.*:SessionConsoleBridge.*" --gtest_brief=1 --gtest_color=no
bin\msvc\debug\Test_AfterNightApp_d.exe --gtest_filter="AboutDialogTest.*:SessionConsoleBridge.*" --gtest_brief=1 --gtest_color=no
```

Result: 11/11 PASS in each configuration from two suites. This covers
About-dialog content contracts and the retained Session Console bridge; it does
not exercise visual layout, external links, file export, or clipboard
integration.

### System Information Windows memory parser

```powershell
bin\msvc\release\Test_SystemInformationPanel.exe --gtest_filter="SystemMemoryInfoParserWindows.*" --gtest_brief=1 --gtest_color=no
bin\msvc\debug\Test_SystemInformationPanel_d.exe --gtest_filter="SystemMemoryInfoParserWindows.*" --gtest_brief=1 --gtest_color=no
```

Result: 3/3 PASS in each configuration from one suite. This covers deterministic
Windows memory-source selection/parsing, not the current machine's live values.

### System Information theme contract and Help routing

```powershell
bin\msvc\release\Test_ProcessFramework.exe --gtest_filter="ThemeTokenBatch40.*:DialogOrchestrationCoordinatorComponent.DialogOrchestrationCoordinator_OpenHelp_*" --gtest_brief=1 --gtest_color=no
bin\msvc\debug\Test_ProcessFramework_d.exe --gtest_filter="ThemeTokenBatch40.*:DialogOrchestrationCoordinatorComponent.DialogOrchestrationCoordinator_OpenHelp_*" --gtest_brief=1 --gtest_color=no
```

Result: 5/5 PASS in each configuration from two suites: two System Information
token/typography cases and three bundled/online Help-routing cases. This does not
launch the dialog or browser.

## Sequential strict documentation gates

Each page and its tracker record were completed before the next page was opened.
All commands ran from the repository root and exited 0.

| Gate | Command | Result |
| --- | --- | --- |
| Troubleshooting And Diagnostics | `build\scripts\msvc\build_user_guide_docs.bat` | PASS; strict MkDocs 6.34 seconds |
| Keyboard Shortcuts | `build\scripts\msvc\build_user_guide_docs.bat` | PASS; strict MkDocs 5.44 seconds |
| Glossary | `build\scripts\msvc\build_user_guide_docs.bat` | PASS; strict MkDocs 5.41 seconds |
| File Formats | `build\scripts\msvc\build_user_guide_docs.bat` | PASS; strict MkDocs 5.46 seconds |
| System Requirements | `build\scripts\msvc\build_user_guide_docs.bat` | PASS; strict MkDocs 5.38 seconds |
| Completion state | `build\scripts\msvc\build_user_guide_docs.bat` | PASS; strict MkDocs 5.93 seconds |
| Review remediation | `build\scripts\msvc\build_user_guide_docs.bat` | PASS; strict MkDocs 5.78 seconds |

## Deterministic audit review

Command used to generate a temporary report outside the worktree:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\audit_user_guide.ps1 -OutputPath $env:TEMP\afternight-ug59-windows-powershell-audit.md
```

Reviewed result: 112 navigated pages, 112 tracker rows, 122 registered built-in
actions, zero menu-parity findings, five later-phase pages with 65 required
headings outstanding, five later-phase rows without current dates/revisions, 72
pages with recorded source drift, zero partial-source records, five unavailable
source reviews, zero generic-boilerplate matches, and 77 canonical process
tables. The accepted report SHA-256 is
`30fd6f73dc52fd947accc8f580e72d5cf8895393458217d7731f7eaf349a7f60`
from the canonical Windows PowerShell run.

The new Troubleshooting page correctly reports the currently dirty Keyboard
Shortcuts, Glossary, File Formats, and System Requirements source pages plus the
`docs/user-guide/mkdocs.yml` navigation source. Those four reviewed Reference
pages separately report no recorded source-path change at the UG-59 revision.

Completion-state validators:

- `powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\validate_ug00_user_guide_foundation.ps1`
  — PASS: 112 tracker rows/17 fields, 60 completed phase-ledger rows, four
  templates, 32 prerequisite groups, and clean foundation whitespace.
- `powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\validate_ug01_user_guide_audits.ps1`
  — review-remediation PASS in 460.7 seconds: baseline hash/expected findings,
  byte-identical reports, Image-menu parity, committed source drift,
  deleted-source drift, and unavailable-source failure fixtures.

## Claim boundary

The evidence supports documentation reachability, exact inventory counts,
source-derived diagnostic behavior, primary-reference consistency, and the
focused test contracts above. It does not establish a real crash, live host
health, successful cross-platform packaging, desktop key delivery, codec/tool
interoperability, visual quality, production output, timing, throughput, or peak
memory. Media is Not needed; independent technical and user reviews remain
deferred to UG-78 and UG-80.

## Final hygiene and scope

- `git diff --check` — PASS.
- `git ls-files --eol` reports `i/lf w/lf` for every modified tracked file.
  Both new Markdown files contain LF only and no trailing whitespace.
- Final scope: 13 changed paths, all under the UG-59 documentation,
  navigation, tracker/prerequisite/plan, audit-baseline/evidence, or
  declarative-validator surfaces. Changed C/C++ paths: zero.
- C/C++ changed-line clang-format, product rebuild, and deploy are not
  applicable to this documentation-only phase.
- Plan-state audit: UG-00 through UG-59 have terminal completion states, no row
  remains In progress, and UG-60 remains Not started.
