param(
)

$ErrorActionPreference = "Stop"

$repo_root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$audit_script = Join-Path $repo_root "build/scripts/audit_user_guide.ps1"
$media_validator = Join-Path $repo_root "build/scripts/validate_user_guide_media.ps1"
$temporary_root = Join-Path ([System.IO.Path]::GetTempPath()) ("afternight-ug01-" + [guid]::NewGuid().ToString('N'))

function Assert-Condition
{
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )

    if (-not $Condition)
    {
        throw $Message
    }
}

function Assert-AuditPageRecord
{
    param(
        [Parameter(Mandatory = $true)][string]$Report,
        [Parameter(Mandatory = $true)][string]$Page,
        [Parameter(Mandatory = $true)][string]$Kind
    )

    $pattern = '(?m)^\| ' + [regex]::Escape($Page) + ' \| ' +
        [regex]::Escape($Kind) +
        ' \| .*? \| (?:Current|Drift|Partial|Unavailable): .*? \| .*? \| .*? \|$'
    Assert-Condition ($Report -match $pattern) `
        "The audit report is missing a valid page-depth record for $Page."
}

function Write-Utf8NoBom
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Text
    )

    [System.IO.Directory]::CreateDirectory((Split-Path -Parent $Path)) | Out-Null
    [System.IO.File]::WriteAllText($Path, $Text, [System.Text.UTF8Encoding]::new($false))
}

function Get-StructuralReportHash
{
    param([Parameter(Mandatory = $true)][string]$Report)

    $normalized = [regex]::Replace(
        $Report,
        '(?m)^\| Pages with recorded source drift \| \d+ \|$',
        '| Pages with recorded source drift | <volatile> |')
    $section_heading = '## Page-depth and source-review report'
    $section_start = $normalized.IndexOf($section_heading, [System.StringComparison]::Ordinal)
    Assert-Condition ($section_start -ge 0) 'Fixture report is missing its page-depth section.'
    $section_end = $normalized.IndexOf("`n## ", $section_start + $section_heading.Length,
        [System.StringComparison]::Ordinal)
    if ($section_end -lt 0)
    {
        $section_end = $normalized.Length
    }
    $section = $normalized.Substring($section_start, $section_end - $section_start)
    $section_lines = @($section -split "`n")
    for ($index = 0; $index -lt $section_lines.Count; ++$index)
    {
        $line = $section_lines[$index]
        if (-not $line.StartsWith('| ') -or
            $line.StartsWith('| Page |') -or
            $line.StartsWith('| --- |'))
        {
            continue
        }
        $cells = @($line -split ' \| ')
        Assert-Condition ($cells.Count -eq 8) "Unexpected fixture page-depth row: $line"
        $cells[5] = '<volatile source drift>'
        $section_lines[$index] = $cells -join ' | '
    }
    $structural_report = $normalized.Substring(0, $section_start) +
        ($section_lines -join "`n") + $normalized.Substring($section_end)
    $bytes = [System.Text.UTF8Encoding]::new($false).GetBytes($structural_report)
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try
    {
        return ([System.BitConverter]::ToString($sha256.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    }
    finally
    {
        $sha256.Dispose()
    }
}

function Invoke-Audit
{
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Output,
        [switch]$CheckBaseline
    )

    $arguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $audit_script,
        '-RepositoryRoot', $Root,
        '-OutputPath', $Output
    )
    if ($CheckBaseline)
    {
        $arguments += '-CheckBaseline'
    }
    & powershell @arguments
    Assert-Condition ($LASTEXITCODE -eq 0) "Audit invocation failed for $Root."
}

try
{
    [System.IO.Directory]::CreateDirectory($temporary_root) | Out-Null

    & powershell -NoProfile -ExecutionPolicy Bypass -File $media_validator -RunFixtures
    Assert-Condition ($LASTEXITCODE -eq 0) `
        'The canonical user-guide media validator or one of its fixtures failed.'

    $actual_one = Join-Path $temporary_root "actual-one.md"
    Invoke-Audit $repo_root $actual_one -CheckBaseline

    $actual_text = [System.IO.File]::ReadAllText($actual_one)
    $required_baseline_fragments = @(
        '| Navigated pages | 113 |',
        '| Tracker page rows | 113 |',
        '| Registered built-in actions | 122 |',
        '| Registered actions with no menu | 0 |',
        '| Direct-wired Process menu actions outside the registry | 3 |',
        '| Direct-wired Image menu actions outside the registry | 34 |',
        '| Parity findings | 0 |',
        '| Pages missing one or more required headings | 3 |',
        '| Required-heading findings | 36 |',
        '| Pages without a current source-review date | 3 |',
        '| Pages without a full reviewed revision | 3 |',
        '| Pages with partial source records | 0 |',
        '| Pages with unavailable source review | 3 |',
        '| Pages matching generic-boilerplate signatures | 0 |',
        '| Canonical | 77 |',
        '| `actionAuto_Gradient_Removal` | Auto Gradient Removal | Background Modelization | processes/background-extraction/auto-gradient-removal.md | Covered |',
        '| `actionDeepSNR` | DeepSNR | Noise Reduction | processes/noise-reduction/deepsnr.md | Covered |',
        '| `actionExport` | Export... | Sequence | interface/saving-and-exporting.md | Covered |',
        '| `actionExtract_Sequence` | Extract Sequence | Sequence | processes/utility/extract-sequence.md | Covered |',
        '| `actionImage_Integration_Lite` | Image Integration Lite | Preprocess | processes/stacking-integration/image-integration-lite.md | Covered |',
        '| `actionImage_Registration` | Image Registration | Preprocess | processes/registration-alignment/image-registration.md | Covered |',
        '| `actionImage_Stacking` | Image Stacking | Preprocess | processes/stacking-integration/image-stacking.md | Covered |',
        '| `actionImage_List_Analyzer` | Image List Analyzer | Utilities | processes/stacking-integration/image-list-analyzer.md | Covered |',
        '| `actionImage_Blend` | Image Blend | Utilities | processes/analysis/image-blend.md | Covered |',
        '| `actionImage_Compare` | Image Compare | Utilities | processes/analysis/image-compare.md | Covered |',
        '| `actionRange_Selection` | Range Selection | Mask Generation | processes/analysis/range-selection.md | Covered |',
        '| `actionCLA_Histogram_Equalization` | CLA Histogram Equalization | Intensity Transformations | processes/sharpening/clahe.md | Covered |',
        '| `actionLocal_Histogram_Equalization` | Local Histogram Equalization | Intensity Transformations | processes/sharpening/local-histogram-equalization.md | Covered |',
        '| `actionLocal_Contrast_Enhancement` | Local Contrast Enhancement | Details | processes/sharpening/local-contrast-enhancement.md | Covered |',
        '| `actionChannelComposition` | Channel Composition | Color Spaces | processes/color-composition/channel-composition.md | Covered |',
        '| `actionLRGB_Combination` | LRGB Combination | Color Spaces | processes/color-composition/lrgb-combination.md | Covered |',
        '| `actionDark_Structure_Enhance` | Dark Structure Enhance | Details | processes/star-operations/dark-structure-enhance.md | Covered |',
        '| `actionProfiled_Starmask_Recombination` | Profiled Starmask Recombination | Star Processing | processes/star-operations/profiled-starmask-recomb.md | Covered |',
        '| `actionStar_Reduction` | Star Reduction | Star Processing | processes/star-operations/star-reduction.md | Covered |',
        '| `actionStarnet_Star_Mask` | Starnet++ Star Mask | Mask Generation | processes/star-operations/starnet-star-mask.md | Covered |',
        '| `actionStarmask_Recombination` | Starmask Recombination | Star Processing | processes/star-operations/starmask-recombination.md | Covered |',
        '| Image | `actionSave_File` | Save File... | interface/saving-and-exporting.md | Covered |',
        '| Image | `actionHistogram` | Histogram | interface/image-histogram.md | Covered |',
        '| Image | `actionStatistics` | Statistics | interface/image-statistics.md | Covered |',
        '| Image | `actionSummary` | Summary | interface/image-summary.md | Covered |'
    )
    foreach ($fragment in $required_baseline_fragments)
    {
        Assert-Condition $actual_text.Contains($fragment) "Baseline report is missing: $fragment"
    }
    Assert-Condition `
        (-not $actual_text.Contains('| Missing |')) `
        'The process-parameter audit must no longer report pages with a missing table.'
    Assert-Condition `
        (-not $actual_text.Contains('| Noncanonical shape |')) `
        'The Windows audit must report no process page with a noncanonical table shape.'
    foreach ($page_record in @(
            [pscustomobject]@{ Page = 'interface/overview.md'; Kind = 'component' },
            [pscustomobject]@{ Page = 'interface/dashboard.md'; Kind = 'component' },
            [pscustomobject]@{ Page = 'interface/image-viewer.md'; Kind = 'component' },
            [pscustomobject]@{ Page = 'interface/process-windows.md'; Kind = 'component' },
            [pscustomobject]@{ Page = 'interface/docking-panels.md'; Kind = 'component' },
            [pscustomobject]@{ Page = 'interface/toolbar-and-menus.md'; Kind = 'component' },
            [pscustomobject]@{ Page = 'interface/log-viewer.md'; Kind = 'component' },
            [pscustomobject]@{ Page = 'interface/image-history.md'; Kind = 'component' },
            [pscustomobject]@{ Page = 'interface/fits-header-viewer.md'; Kind = 'component' },
            [pscustomobject]@{ Page = 'interface/masks.md'; Kind = 'component' },
            [pscustomobject]@{ Page = 'index.md'; Kind = 'component' },
            [pscustomobject]@{ Page = 'processes/index.md'; Kind = 'reference' },
            [pscustomobject]@{ Page = 'reference/glossary.md'; Kind = 'reference' },
            [pscustomobject]@{ Page = 'extensions/using-extensions.md'; Kind = 'component' },
            [pscustomobject]@{ Page = 'extensions/extension-marketplace.md'; Kind = 'component' },
            [pscustomobject]@{ Page = 'reference/file-formats.md'; Kind = 'reference' },
            [pscustomobject]@{ Page = 'reference/system-requirements.md'; Kind = 'reference' },
            [pscustomobject]@{ Page = 'reference/keyboard-shortcuts.md'; Kind = 'reference' },
            [pscustomobject]@{ Page = 'reference/troubleshooting.md'; Kind = 'reference' },
            [pscustomobject]@{ Page = 'workflows/broadband-imaging.md'; Kind = 'workflow' },
            [pscustomobject]@{ Page = 'workflows/narrowband-imaging.md'; Kind = 'workflow' }))
    {
        Assert-AuditPageRecord $actual_text $page_record.Page $page_record.Kind
    }


    $fixture_root = Join-Path $temporary_root "drift-fixture"
    [System.IO.Directory]::CreateDirectory($fixture_root) | Out-Null
    [System.IO.Directory]::CreateDirectory((Join-Path $fixture_root "docs/user-guide/audits")) | Out-Null
    [System.IO.Directory]::CreateDirectory((Join-Path $fixture_root "docs/user-guide/templates")) | Out-Null
    Copy-Item -LiteralPath (Join-Path $repo_root "docs/user-guide/audits/user-guide-audit-config.json") `
        -Destination (Join-Path $fixture_root "docs/user-guide/audits/user-guide-audit-config.json")
    foreach ($template_name in @(
        'COMPONENT_PAGE_TEMPLATE.md',
        'PROCESS_PAGE_TEMPLATE.md',
        'REFERENCE_PAGE_TEMPLATE.md',
        'WORKFLOW_PAGE_TEMPLATE.md'))
    {
        Copy-Item -LiteralPath (Join-Path $repo_root "docs/user-guide/templates/$template_name") `
            -Destination (Join-Path $fixture_root "docs/user-guide/templates/$template_name")
    }

    Write-Utf8NoBom (Join-Path $fixture_root "docs/user-guide/mkdocs.yml") @'
nav:
  - Processes:
      - Test Process: processes/test-process.md
'@
    [System.IO.Directory]::CreateDirectory(
        (Join-Path $fixture_root "docs/user-guide/docs/en/processes")) | Out-Null
    Copy-Item -LiteralPath (Join-Path $repo_root "docs/user-guide/templates/PROCESS_PAGE_TEMPLATE.md") `
        -Destination (Join-Path $fixture_root "docs/user-guide/docs/en/processes/test-process.md")
    Write-Utf8NoBom (Join-Path $fixture_root "src/AfterNightProcesses/app/ProcessRegistryBuiltins.cpp") @'
void registerFixture()
{
    AN_REGISTER_PROCESS_SINGLE(kCategoryFixture, actionTest_Process, executeTestProcess);
}
'@
    Write-Utf8NoBom (Join-Path $fixture_root "src/AfterNightControls/app/AfterNightAppUiBuilder.h") @'
menuImage->addAction(menuImageFixture->menuAction());
menuImageFixture->addAction(actionImage_Only);
menuProcess->addAction(menuFixture->menuAction());
menuFixture->addAction(actionTest_Process);
actionImage_Only->setText(QCoreApplication::translate("AfterNightAppClass", "Image Only", nullptr));
actionTest_Process->setText(QCoreApplication::translate("AfterNightAppClass", "Test Process", nullptr));
menuImage->setTitle(QCoreApplication::translate("AfterNightAppClass", "Image", nullptr));
menuImageFixture->setTitle(QCoreApplication::translate("AfterNightAppClass", "Image Fixture", nullptr));
menuProcess->setTitle(QCoreApplication::translate("AfterNightAppClass", "Process", nullptr));
menuFixture->setTitle(QCoreApplication::translate("AfterNightAppClass", "Fixture", nullptr));
'@
    Write-Utf8NoBom (Join-Path $fixture_root "src/Test.cpp") "int fixture_value = 1;`n"
    Write-Utf8NoBom (Join-Path $fixture_root "docs/user-guide/DOCUMENTATION_COMPLETION_TRACKER.md") @'
| Page | Owner | Content state | Media state | Source review | Source-review date | Reviewed revision | Dirty reviewed sources | Source files and tests | UI reachability | Parameter/action coverage | Algorithm/behavior coverage | Example/workflow coverage | Technical review | User review | Last strict build | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| [Test Process](docs/en/processes/test-process.md) | Test | Draft | Not needed | Current | 2026-07-10 | PENDING | None | `src/Test.cpp` | Checked | Checked | Checked | Checked | Not requested | Not requested | PASS | Fixture |
'@

    Push-Location $fixture_root
    try
    {
        & git init -q
        & git config user.email "ug01-audit@example.invalid"
        & git config user.name "UG-01 Audit Fixture"
        & git add .
        & git commit -q -m "fixture baseline"
        $reviewed_revision = (& git rev-parse HEAD).Trim()

        $tracker_fixture = Join-Path $fixture_root "docs/user-guide/DOCUMENTATION_COMPLETION_TRACKER.md"
        $tracker_text = [System.IO.File]::ReadAllText($tracker_fixture).Replace('PENDING', $reviewed_revision)
        Write-Utf8NoBom $tracker_fixture $tracker_text
        & git add docs/user-guide/DOCUMENTATION_COMPLETION_TRACKER.md
        & git commit -q -m "fixture review record"
    }
    finally
    {
        Pop-Location
    }

    $current_fixture_report = Join-Path $temporary_root "current-fixture-report.md"
    Invoke-Audit $fixture_root $current_fixture_report
    $current_fixture_text = [System.IO.File]::ReadAllText($current_fixture_report)
    Assert-Condition ($current_fixture_text.Contains('Current: no recorded source path changed')) `
        'The structural-baseline fixture did not begin with current source evidence.'
    $fixture_baseline_hash = Get-StructuralReportHash $current_fixture_text
    Write-Utf8NoBom (Join-Path $fixture_root "docs/user-guide/audits/user-guide-audit-baseline.json") `
        ("{`n  `"schemaVersion`": 2,`n  `"reportHashScope`": `"structural-report-v1`",`n" +
         "  `"reportSha256`": `"$fixture_baseline_hash`"`n}`n")

    Write-Utf8NoBom (Join-Path $fixture_root "src/Test.cpp") "int fixture_value = 2;`n"
    $fixture_report = Join-Path $temporary_root "fixture-report.md"
    Invoke-Audit $fixture_root $fixture_report -CheckBaseline
    $fixture_text = [System.IO.File]::ReadAllText($fixture_report)
    Assert-Condition ($fixture_text.Contains('Drift: changed since revision: src/Test.cpp')) `
        'The source-drift fixture did not identify src/Test.cpp.'
    Push-Location $fixture_root
    try
    {
        & git add .
        & git commit -q -m "fixture source drift"
    }
    finally
    {
        Pop-Location
    }
    Assert-Condition ($fixture_text.Contains(
        '| IMAGE_MENU_DESTINATION_UNRESOLVED | actionImage_Only | direct-wired Image menu action has no guide override |')) `
        'The Image-menu fixture did not identify an unmapped direct-wired action.'

    $untracked_source = Join-Path $fixture_root "src/Untracked.cpp"
    Write-Utf8NoBom $untracked_source "int untracked_fixture = 1;`n"
    $tracker_fixture = Join-Path $fixture_root "docs/user-guide/DOCUMENTATION_COMPLETION_TRACKER.md"
    $tracked_fixture_text = [System.IO.File]::ReadAllText($tracker_fixture)
    Write-Utf8NoBom $tracker_fixture ($tracked_fixture_text.Replace(
        '`src/Test.cpp`', '`src/Test.cpp`; `src/Untracked.cpp`'))

    $untracked_fixture_report = Join-Path $temporary_root "untracked-fixture-report.md"
    Invoke-Audit $fixture_root $untracked_fixture_report
    $untracked_fixture_text = [System.IO.File]::ReadAllText($untracked_fixture_report)
    Assert-Condition ($untracked_fixture_text.Contains(
        'Drift: changed since revision: src/Test.cpp, src/Untracked.cpp')) `
        'The untracked-source fixture did not include the new reviewed path in stable source drift.'

    Push-Location $fixture_root
    try
    {
        & git add .
        & git commit -q -m "fixture untracked source commit"
    }
    finally
    {
        Pop-Location
    }

    $committed_fixture_report = Join-Path $temporary_root "committed-fixture-report.md"
    Invoke-Audit $fixture_root $committed_fixture_report
    $untracked_fixture_bytes = [System.IO.File]::ReadAllBytes($untracked_fixture_report)
    $committed_fixture_bytes = [System.IO.File]::ReadAllBytes($committed_fixture_report)
    Assert-Condition ($untracked_fixture_bytes.Length -eq $committed_fixture_bytes.Length) `
        'Source-drift report length changed when the reviewed working-tree changes were committed.'
    Assert-Condition `
        ([System.Linq.Enumerable]::SequenceEqual($untracked_fixture_bytes, $committed_fixture_bytes)) `
        'Source-drift report changed when the reviewed working-tree changes were committed.'

    Remove-Item -LiteralPath $untracked_source -Force
    Write-Utf8NoBom $tracker_fixture $tracked_fixture_text
    Push-Location $fixture_root
    try
    {
        & git add --all
        & git commit -q -m "fixture source drift cleanup"
    }
    finally
    {
        Pop-Location
    }

    Write-Utf8NoBom (Join-Path $fixture_root "src/AfterNightControls/app/AfterNightAppUiBuilder.h") @'
menuProcess->addAction(menuFixture->menuAction());
menuFixture->addAction(actionTest_Process);
actionTest_Process->setText(QCoreApplication::translate("AfterNightAppClass", "Test Process", nullptr));
menuProcess->setTitle(QCoreApplication::translate("AfterNightAppClass", "Process", nullptr));
menuFixture->setTitle(QCoreApplication::translate("AfterNightAppClass", "Fixture", nullptr));
'@
    Push-Location $fixture_root
    try
    {
        & git rm -q src/Test.cpp
        & git add .
        & git commit -q -m "fixture source deletion"
    }
    finally
    {
        Pop-Location
    }

    $deleted_fixture_report = Join-Path $temporary_root "deleted-fixture-report.md"
    Invoke-Audit $fixture_root $deleted_fixture_report
    $deleted_fixture_text = [System.IO.File]::ReadAllText($deleted_fixture_report)
    Assert-Condition ($deleted_fixture_text.Contains(
        'Drift: changed since revision: src/Test.cpp')) `
        'The deleted-source fixture did not report the reviewed path as changed.'

    $tracker_text = [System.IO.File]::ReadAllText($tracker_fixture).Replace(
        '`src/Test.cpp`', '`src/Missing.cpp`')
    Write-Utf8NoBom $tracker_fixture $tracker_text
    Push-Location $fixture_root
    try
    {
        & git add .
        & git commit -q -m "fixture unresolved source record"
    }
    finally
    {
        Pop-Location
    }

    $unresolved_fixture_report = Join-Path $temporary_root "unresolved-fixture-report.md"
    Invoke-Audit $fixture_root $unresolved_fixture_report
    $unresolved_fixture_text = [System.IO.File]::ReadAllText($unresolved_fixture_report)
    Assert-Condition ($unresolved_fixture_text.Contains(
        'Unavailable: recorded paths do not resolve at the reviewed revision or in the current tree: src/Missing.cpp')) `
        'The unresolved-source fixture did not report unavailable source evidence.'

    $saved_error_action_preference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & powershell -NoProfile -ExecutionPolicy Bypass -File $audit_script `
        -RepositoryRoot $fixture_root -OutputPath (Join-Path $temporary_root "unresolved-fail-report.md") `
        -FailOnFindings *> $null
    $fail_on_findings_exit_code = $LASTEXITCODE
    $ErrorActionPreference = $saved_error_action_preference
    Assert-Condition ($fail_on_findings_exit_code -ne 0) `
        '-FailOnFindings accepted an unavailable reviewed source path.'

    # Pages reviewed at one revision whose source paths together are longer than one Windows command line, which
    # starts a program with at most 32,767 characters: the audit still reads every path at that revision.
    $long_source_paths = @(foreach ($index in 1..360)
    {
        'src/ReviewedAtOneRevision/' + ('LongComponentDirectory' * 3) + ('/Source{0:D3}.cpp' -f $index)
    })
    foreach ($long_source_path in $long_source_paths)
    {
        Write-Utf8NoBom (Join-Path $fixture_root $long_source_path) "int long_source_fixture = 1;`n"
    }
    Push-Location $fixture_root
    try
    {
        & git add .
        & git commit -q -m "fixture sources beyond one command line"
        $long_reviewed_revision = (& git rev-parse HEAD).Trim()
    }
    finally
    {
        Pop-Location
    }
    $long_source_record = @($long_source_paths | ForEach-Object { '`' + $_ + '`' }) -join '; '
    $tracker_text = [System.IO.File]::ReadAllText($tracker_fixture).Replace(
        '`src/Missing.cpp`', $long_source_record).Replace($reviewed_revision, $long_reviewed_revision)
    Write-Utf8NoBom $tracker_fixture $tracker_text
    Push-Location $fixture_root
    try
    {
        & git add .
        & git commit -q -m "fixture review record beyond one command line"
    }
    finally
    {
        Pop-Location
    }

    $long_fixture_report = Join-Path $temporary_root "long-source-list-fixture-report.md"
    Invoke-Audit $fixture_root $long_fixture_report
    $long_fixture_text = [System.IO.File]::ReadAllText($long_fixture_report)
    Assert-Condition ($long_fixture_text.Contains('Current: no recorded source path changed')) `
        'The fixture reviewed against more source paths than one command line holds did not read them all.'

    Write-Output ('UG-01 audit validation PASS: structural baseline and expected findings match; ' +
        'source edits preserve the structural baseline; Image-menu parity, stable pre/post-commit source drift, ' +
        'deleted-source drift/unavailable-source failure and longer-than-one-command-line source list fixtures ' +
        'passed; canonical media validation and eight negative media fixtures passed.')
}
finally
{
    if (Test-Path -LiteralPath $temporary_root)
    {
        Remove-Item -LiteralPath $temporary_root -Recurse -Force
    }
}
