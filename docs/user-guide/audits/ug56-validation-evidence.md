# UG-56 validation evidence

Date: 2026-07-19

Reviewed source revision: `b6d8ec0354a0db5bb190d649cfb8b8453e2541ea`

Dirty-source state: product and test sources were clean at UG-56 start. Review
remediation changes
`src/AfterNightControls/controls/settings/ExtensionStatusPage.cpp` and
`src/AfterNightTests/Test_AfterNightApp/test_extension_status_page.cpp` against
that revision. The UG-56 guide, tracker, prerequisite, plan, audit fixture, and
validator files were already dirty as the completed phase under review.

## Red-to-green regression

The test expectation was changed before production logic. After the Release test
build, this exact command was run from `bin/msvc/release`:

```powershell
$env:QT_QPA_PLATFORM='offscreen'; .\Test_AfterNightApp.exe --gtest_filter=ExtensionStatusPageTest.ManualDeploymentWithMissingEnvironmentIsNotShownAvailable --gtest_brief=1 --gtest_color=no
```

Red result: exit code 1; 1 test ran, 0 passed, and the test failed because the
Missing manual environment was still labelled **Available**. After removing the
status-page exception, the same command returned exit code 0 and passed 1/1 in
25 ms. The completed suite validation below covers that regression plus the
existing-path/Missing-state regression in both Release and Debug.

## MSVC test builds

Working directory: `build/scripts/msvc`.

```cmd
09a-build_afternight_tests_release.bat
09b-build_afternight_tests_debug.bat
```

Both commands returned exit code 0. Their MSBuild summaries reported Build
succeeded with 0 warnings and 0 errors, and their test-runtime/extension-host
deployment steps completed.

The ordinary main-project build/deploy commands were then run from the same
working directory:

```cmd
05a-build_afternight_release.bat
05b-build_afternight_debug.bat
```

Both returned exit code 0. Each final solution summary reported Build succeeded
with 0 warnings and 0 errors; the application post-build Qt deployment completed
for its configuration.

From `bin/msvc/release`, a deployed-app startup smoke set
`QT_QPA_PLATFORM=offscreen`, launched `AfterNight.exe`, verified that it remained
running for five seconds, and then stopped that test process. The smoke returned
exit code 0.

## Release focused tests

Working directory: `bin/msvc/release`.

Environment setup:

```powershell
$env:QT_QPA_PLATFORM='offscreen'
Remove-Item Env:AFTERNIGHT_EXTENSION_REPOSITORY_ROOT -ErrorAction SilentlyContinue
```

Commands and exact results:

```powershell
.\Test_AfterNightApp.exe '--gtest_filter=ExtensionStatusPageTest.*' --gtest_brief=1 --gtest_color=no
```

Exit code 0: 21 tests from 1 test suite ran; 21 passed.

```powershell
.\Test_AfterNightApp.exe '--gtest_filter=ExtensionManagerPageTest.*:SettingsDialogTest.GeneralSettingsPage_SourceDeclaresUpdatesSectionDefaultsAndPersistence' --gtest_brief=1 --gtest_color=no
```

Exit code 0: 59 tests from 2 test suites ran; 59 passed.

```powershell
.\Test_ProcessFramework.exe '--gtest_filter=ExtensionManagerTest.*Environment*:ExtensionProtocolBoundaryTest.*:ExtensionProtocolTest.*:ExtensionSettingsSectionTest.*' --gtest_brief=1 --gtest_color=no
```

Exit code 0: 49 tests from 4 test suites ran; 49 passed.

```powershell
.\Test_ProcessFramework.exe '--gtest_filter=ExtensionManagerTest.*Transaction*:ExtensionManagerTest.*Repository*:ExtensionManagerTest.*Startup*:ExtensionManagerTest.*Cache*:ExtensionManagerTest.*Offline*:ExtensionManagerTest.SafePackageArchiveExtractionRollsBackAfterFailedExtraction-ExtensionManagerTest.VeraLuxRepositoryPackageInstallsProvisionsAndCachesPerProcessSchemas' --gtest_brief=1 --gtest_color=no
```

Exit code 0: 39 tests from 1 test suite ran; 38 tests passed and 1 test was
skipped. The skipped test was
`ExtensionManagerTest.ExternalExtensionRepositoryPassesAppSidePackageValidator`
because `AFTERNIGHT_EXTENSION_REPOSITORY_ROOT` was unavailable. The opt-in
VeraLux online provisioning smoke is explicitly excluded from this bounded
source/fixture validation.

## Debug focused tests

Working directory: `bin/msvc/debug`. The same environment setup was applied.

```powershell
.\Test_AfterNightApp_d.exe '--gtest_filter=ExtensionStatusPageTest.*' --gtest_brief=1 --gtest_color=no
```

Exit code 0: 21 tests from 1 test suite ran; 21 passed.

```powershell
.\Test_AfterNightApp_d.exe '--gtest_filter=ExtensionManagerPageTest.*:SettingsDialogTest.GeneralSettingsPage_SourceDeclaresUpdatesSectionDefaultsAndPersistence' --gtest_brief=1 --gtest_color=no
```

Exit code 0: 59 tests from 2 test suites ran; 59 passed.

```powershell
.\Test_ProcessFramework_d.exe '--gtest_filter=ExtensionManagerTest.*Environment*:ExtensionProtocolBoundaryTest.*:ExtensionProtocolTest.*:ExtensionSettingsSectionTest.*' --gtest_brief=1 --gtest_color=no
```

Exit code 0: 49 tests from 4 test suites ran; 49 passed.

```powershell
.\Test_ProcessFramework_d.exe '--gtest_filter=ExtensionManagerTest.*Transaction*:ExtensionManagerTest.*Repository*:ExtensionManagerTest.*Startup*:ExtensionManagerTest.*Cache*:ExtensionManagerTest.*Offline*:ExtensionManagerTest.SafePackageArchiveExtractionRollsBackAfterFailedExtraction-ExtensionManagerTest.VeraLuxRepositoryPackageInstallsProvisionsAndCachesPerProcessSchemas' --gtest_brief=1 --gtest_color=no
```

Exit code 0: 39 tests from 1 test suite ran; 38 tests passed and 1 test was
skipped for the same unavailable external-repository prerequisite.

## Full Release test suite

Working directory: `bin/msvc/release`. The same environment setup was applied.
The repository batch runner was first run with `cmd /c run_tests.bat`; it
completed in 986.1 seconds. Because that batch file's failure branch does not
propagate a nonzero process exit code, the Release executables were then rerun
with the following failure-propagating PowerShell wrapper:

```powershell
$failed = @(); $tests = Get-ChildItem -LiteralPath . -Filter 'Test_*.exe' | Sort-Object Name; foreach ($test in $tests) { $sw = [Diagnostics.Stopwatch]::StartNew(); & $test.FullName '--gtest_brief=1' '--gtest_color=no' 2>&1 | ForEach-Object { if ($_ -match '^\[(=+|  PASSED  |  FAILED  |  SKIPPED )\]' -or $_ -match '^YOU HAVE ' -or $_ -match '^Note: Google Test filter') { $_ } }; $code = $LASTEXITCODE; $sw.Stop(); [pscustomobject]@{ Test = $test.Name; ExitCode = $code; Seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1) } | Format-Table -AutoSize; if ($code -ne 0) { $failed += $test.Name } }; if ($failed.Count -gt 0) { Write-Error ('Failed test executables: ' + ($failed -join ', ')); exit 1 }; Write-Output ('All ' + $tests.Count + ' Release test executables passed.')
```

Exit code 0 after 944.1 seconds: all 24 Release test executables passed. Across
those executables, 7,107 tests ran; 7,095 passed and 12 were skipped because
their optional external prerequisites were unavailable. No test failed.

## Formatting and documentation

Working directory: repository root unless stated otherwise.

```cmd
build\scripts\msvc\check_clang_format.bat origin/main
```

Exit code 0 using clang-format 20.1.8. The helper reported no changed C/C++
lines against its merge-base selection. Because the remediation was uncommitted,
the two affected ranges were also checked directly:

```powershell
$clang='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\bin\clang-format.exe'; & $clang --dry-run --Werror --style=file --lines=225:260 --lines=1068:1090 'src/AfterNightControls/controls/settings/ExtensionStatusPage.cpp'; if($LASTEXITCODE-ne0){exit $LASTEXITCODE}; & $clang --dry-run --Werror --style=file --lines=588:615 --lines=653:688 'src/AfterNightTests/Test_AfterNightApp/test_extension_status_page.cpp'
```

Exit code 0 with no diagnostics. A Linux/CI format check was not run because no
`bash` executable is available in this Windows environment.

From `build/scripts/msvc`:

```cmd
build_user_guide_docs.bat
```

The initial remediation pass returned exit code 0 and completed strict MkDocs in
5.75 seconds. The final pass after page-evidence synchronization also returned
exit code 0 and completed in 5.06 seconds.

## Guide foundation and audit validators

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build/scripts/validate_ug00_user_guide_foundation.ps1
```

Exit code 0: 111 tracker rows/17 fields, 57 completed phase-ledger rows, 4
templates, 32 prerequisite groups, and foundation whitespace clean.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build/scripts/validate_ug01_user_guide_audits.ps1
```

Final exit code 0 after 431.1 seconds: baseline hash and expected findings match;
the two local reports are byte-identical; Image-menu parity, committed-source
drift, deleted-source drift, and unavailable-source failure fixtures passed.
Three preceding runs of the same command completed their two full audits and then
failed on stale declarative UG-56 assertions (old Glossary drift wording, old
signature terminology, and an over-literal evidence-result fragment). Those
assertions were synchronized with the corrected guide/evidence before the final
passing run.
