# UG-58 Validation Evidence

This record retains the exact validation scope for File Formats and System
Requirements at revision `bc552c72cb6f4a06ee987b65308c1c9101e00db3`.

## File Formats source audit

The source audit covered the current still-codec registry and implementations,
metadata writer, save-option settings, open/save dialog filters and suffix
normalization, native and FFmpeg-backed sequence dispatch, same-family sequence
copy, Sequence Export, and their focused tests. It confirms the page's single
authoritative matrix for input/output extensions, sample conversion,
compression, metadata, and sequence representation.

Review remediation confirmed that **Debayer CFA** is initialized Off whenever
Sequence Export opens, copied into the immutable request used by that export,
and not written to either the global Export settings or the dialog's persisted
format/codec/resolution JSON. `export/debayerCfa` is not a current settings key.

The unresolved-GUI checklist is empty. Source and focused tests establish the
documented routes and static/runtime contracts; the page makes no claim about
rendered dialog layout, a particular camera or video stream, third-party
interoperability, visual quality, compression ratio, production timing, or peak
memory. No application or approved-data execution was required.

## File Formats focused MSVC tests

The commands were run from each deployed `bin/msvc/<config>` directory. UI
executables used `QT_QPA_PLATFORM=offscreen`; Debug executable names use `_d`.

### Codec, precision, and metadata cases

```powershell
Test_ImageCodecs.exe --gtest_filter="ImageCodecsTest.FITS_*:ImageCodecsTest.PNG_*:ImageCodecsTest.JPEG_*:ImageCodecsTest.TIFF_*:ImageCodecsTest.XISF_*:ImageCodecsTest.EXIF_*:ImageCodecsTest.UnicodePaths_*:ImageCodecsTest.Exiv2_*:ImageCodecsTest.SequenceExtraction*:ImageCodecsTest.SER_*:*ParameterizedCodecTest.RoundTrip*" --gtest_brief=1 --gtest_color=no
Test_ImageCodecs_d.exe --gtest_filter="ImageCodecsTest.FITS_*:ImageCodecsTest.PNG_*:ImageCodecsTest.JPEG_*:ImageCodecsTest.TIFF_*:ImageCodecsTest.XISF_*:ImageCodecsTest.EXIF_*:ImageCodecsTest.UnicodePaths_*:ImageCodecsTest.Exiv2_*:ImageCodecsTest.SequenceExtraction*:ImageCodecsTest.SER_*:*ParameterizedCodecTest.RoundTrip*" --gtest_brief=1 --gtest_color=no
```

- Release: 37/37 PASS in 17.4 seconds.
- Debug: 37/37 PASS in 41.6 seconds.
- Limit: several broad compression/EXIF/Exiv2 cases are informational
  scaffolds rather than strong round-trip assertions. The page therefore keeps
  generic JPEG/TIFF/PNG EXIF success, external interoperability, and exact
  compression outcome qualified; current codec source is authoritative for
  option dispatch and conversions.

### File-management and sequence suffix cases

```powershell
Test_AfterNightApp.exe --gtest_filter="AfterNightAppFileSuite.*" --gtest_brief=1 --gtest_color=no
Test_AfterNightApp_d.exe --gtest_filter="AfterNightAppFileSuite.*" --gtest_brief=1 --gtest_color=no
```

- Release: 11/11 PASS.
- Debug: 11/11 PASS.

### Sequence Export and cancelled-save contracts

```powershell
Test_ProcessFramework.exe --gtest_filter="ThemeTokenBatch39.ExportSequenceSuffixNormalizationMatchesSelectedWriter:UiControlAppContextDecouplingTest.ExportSequenceSnapshotsUiAndLocksDialogBeforeWorkerStarts:UiControlAppContextDecouplingTest.CancelledSequenceSaveDoesNotNotifySavedState" --gtest_brief=1 --gtest_color=no
Test_ProcessFramework_d.exe --gtest_filter="ThemeTokenBatch39.ExportSequenceSuffixNormalizationMatchesSelectedWriter:UiControlAppContextDecouplingTest.ExportSequenceSnapshotsUiAndLocksDialogBeforeWorkerStarts:UiControlAppContextDecouplingTest.CancelledSequenceSaveDoesNotNotifySavedState" --gtest_brief=1 --gtest_color=no
```

- Release: 3/3 PASS.
- Debug: 3/3 PASS.

### Open-filter and suffix classification

```powershell
Test_ImageIntegration.exe --gtest_filter="FileDialogUtils.SupportedImageSuffixesAreClassified:FileDialogUtils.SupportedImageOpenFiltersIncludeRawAndLessCommonCodecExtensions:FileDialogUtils.SupportedSequenceSuffixesAreClassified" --gtest_brief=1 --gtest_color=no
Test_ImageIntegration_d.exe --gtest_filter="FileDialogUtils.SupportedImageSuffixesAreClassified:FileDialogUtils.SupportedImageOpenFiltersIncludeRawAndLessCommonCodecExtensions:FileDialogUtils.SupportedSequenceSuffixesAreClassified" --gtest_brief=1 --gtest_color=no
```

- Release: 3/3 PASS.
- Debug: 3/3 PASS.

## File Formats sequential documentation gate

`build\scripts\msvc\build_user_guide_docs.bat` passed strict MkDocs validation
in 5.90 seconds after the page, tracker, prerequisite override, and this evidence
record were synchronized.

## System Requirements source audit

The source audit covered current release artifacts and CI runners, the x64 and
Qt configuration, Windows and Linux packaging, the numbered source-build and
deploy scripts, bundled runtime versions, the documented 1080p/720p design
targets and window-geometry source test, the System Information route/report,
external-tool version checks, managed Gaia/SPCC catalog descriptors, and their
focused tests. Official Qt and AppImage documentation supplies the upstream
Windows-platform and FUSE boundaries; AfterNight's narrower packaged and
verified targets remain explicit.

The project does not encode or publish a certified minimum CPU core count, RAM
capacity, free-space amount, or discrete-GPU model. AVX2 is an enforced
instruction-set floor: the production Engine and application targets select
AVX2, with explicit `/arch:AVX2` and `-mavx2` compiler options in the main
Premake configuration. The page therefore removes the fixed capacity
numbers while distinguishing enforced architecture/ISA, verified package
targets, responsive-layout validation targets, planning guidance, and
transparent decoded-image arithmetic. The unresolved-GUI checklist is empty:
the page does not claim a particular machine's reported values, rendered dialog
layout, clipboard/file-dialog integration, production throughput, or measured
peak memory. No application, installer, AppImage, network service, external
tool, catalog download, or approved project was launched.

## System Requirements focused MSVC tests

The commands were run from each deployed `bin/msvc/<config>` directory. Debug
executable names use `_d`; Qt UI cases used `QT_QPA_PLATFORM=offscreen`.

### System-memory snapshot and System Information styling

```powershell
Test_SystemInformationPanel.exe --gtest_brief=1 --gtest_color=no
Test_SystemInformationPanel_d.exe --gtest_brief=1 --gtest_color=no

Test_ProcessFramework.exe --gtest_filter="ThemeTokenBatch40.Source_SystemInformationDialogUsesTokenTypography:ThemeTokenBatch40.Runtime_SystemInformationDialogUsesTokenTypography" --gtest_brief=1 --gtest_color=no
Test_ProcessFramework_d.exe --gtest_filter="ThemeTokenBatch40.Source_SystemInformationDialogUsesTokenTypography:ThemeTokenBatch40.Runtime_SystemInformationDialogUsesTokenTypography" --gtest_brief=1 --gtest_color=no
```

- System-memory snapshot: 3/3 PASS in Release and 3/3 PASS in Debug. The MSVC
  executable covers the Windows snapshot helpers; Linux `/proc/meminfo` parser
  cases are platform-gated and were not run on this host.
- Typography contract: 2/2 PASS in Release and 2/2 PASS in Debug.
- Limit: route, displayed field inventory, copy/export behavior, storage
  enumeration, and report contents are source-derived rather than exercised by
  these focused tests.

### Optional external-tool version contracts

```powershell
Test_ProcessFramework.exe --gtest_filter="StarNetCliToolsTest.*:DeepSNRCliToolsTest.*" --gtest_brief=1 --gtest_color=no
Test_ProcessFramework_d.exe --gtest_filter="StarNetCliToolsTest.*:DeepSNRCliToolsTest.*" --gtest_brief=1 --gtest_color=no
```

- Release: 13/13 PASS.
- Debug: 13/13 PASS.
- Limit: these tests establish parsing, minimum-version gates, arguments, and
  path/stride helpers; they do not execute either external product or validate
  models, drivers, licenses, inference output, timing, or memory.

### Advertised SPCC full-catalog descriptor

```powershell
Test_ImageIntegration.exe --gtest_filter="SpccCatalogSettingsTest.AdvertisesApprovedFullCatalogPackByDefault" --gtest_brief=1 --gtest_color=no
Test_ImageIntegration_d.exe --gtest_filter="SpccCatalogSettingsTest.AdvertisesApprovedFullCatalogPackByDefault" --gtest_brief=1 --gtest_color=no
```

- Release: 1/1 PASS.
- Debug: 1/1 PASS.
- Limit: this verifies the advertised descriptor, not a live catalog download,
  installation, repair, regional query, output, or performance result. Gaia
  pack values and download/repair behavior remain source-derived.

## System Requirements sequential documentation gate

`build\scripts\msvc\build_user_guide_docs.bat` passed strict MkDocs validation
in 5.08 seconds after the page, tracker, prerequisite override, and this evidence
record were synchronized. An immediately preceding attempt correctly rejected
five stale related-page paths from the page's previous draft; those paths were replaced
with current navigated targets before this passing gate.

## Documentation and audit validation

```powershell
build\scripts\msvc\build_user_guide_docs.bat
powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\validate_ug00_user_guide_foundation.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\audit_user_guide.ps1 -OutputPath <temporary-report> -CheckBaseline
powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\validate_ug01_user_guide_audits.ps1
git diff --check
```

- Final synchronized strict MkDocs before terminal metadata: PASS in 5.12
  seconds. The initial System Requirements attempt correctly failed on five
  stale related-page paths; all were replaced before a passing gate.
- Completion-state UG-00: PASS with 111 tracker rows/17 fields, 59 completed
  phase-ledger rows, four templates, and 32 prerequisite groups.
- Terminal checked audit regeneration: PASS in 190.7 seconds with 111
  navigated/tracked pages, zero parity findings, five pages awaiting later phases, 65
  required-heading findings, 72 recorded-source drift pages, zero partial
  source records, and SHA-256
  `39635b40365d1ac183ee0575a3a11302fe9a287520dd86947e1bc37f2435ea3b`.
- Full UG-01 repeatability/parity/drift validator: PASS in 385.9 seconds. Three
  preceding complete attempts were intentionally not accepted as final: a
  historical Home-row assertion omitted its current dirty File Formats
  dependency; the File Formats declarative gate exposed and prompted addition
  of the missing default-off CFA debayer matrix row; and one evidence assertion
  crossed a Markdown line wrap. Each issue was corrected and the entire
  validator was rerun with explicit exit-code propagation.
- No C/C++ file changed, so product rebuild/deploy and changed-line
  clang-format validation are not applicable.

## Review remediation validation (2026-07-20)

The review corrections add the AVX2 production requirement and controlling
`build/premake/afternight-pm5.lua` source, remove the nonexistent persisted
`export/debayerCfa` contract, and add declarative regressions for both findings.

- `build\scripts\msvc\build_user_guide_docs.bat`: PASS; strict MkDocs built
  the remediated pages in 6.23 seconds.
- `powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\validate_ug00_user_guide_foundation.ps1`:
  PASS with 111 tracker rows/17 fields, 59 completed phase-ledger rows, four
  templates, and 32 prerequisite groups.
- Focused page/tracker/prerequisite/plan/evidence remediation assertions: 10/10
  PASS.
- `powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\audit_user_guide.ps1 -CheckBaseline`:
  PASS in 210.6 seconds; the canonical counts and SHA-256 remain unchanged.
- `powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\validate_ug01_user_guide_audits.ps1`:
  PASS in 439.3 seconds. One preceding complete attempt correctly failed because
  the new tracker regression expected a hyphenated summary rather than the
  tracker's exact "snapshotted for one export, and not persisted" wording; the
  assertion was aligned to the authoritative tracker text and the entire gate
  was rerun.
- `git diff --check`: PASS before terminal evidence synchronization.
