$ErrorActionPreference = "Stop"

$repo_root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

function Assert-Condition
{
    param(
        [Parameter(Mandatory = $true)]
        [bool]$Condition,

        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    if (-not $Condition)
    {
        throw $Message
    }
}

function Read-RepositoryFile
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )

    $path = Join-Path $repo_root $RelativePath
    Assert-Condition (Test-Path -LiteralPath $path -PathType Leaf) "Missing required file: $RelativePath"
    return Get-Content -LiteralPath $path -Encoding UTF8
}

$tracker_path = "docs/user-guide/DOCUMENTATION_COMPLETION_TRACKER.md"
$tracker_lines = Read-RepositoryFile $tracker_path
$tracker_rows = @($tracker_lines | Where-Object { $_ -match '^\| \[' })
Assert-Condition ($tracker_rows.Count -eq 113) "Expected 113 tracker page rows; found $($tracker_rows.Count)."

$allowed_media_states = @(
    "Not assessed",
    "Not needed",
    "Planned",
    "Captured",
    "Placed",
    "Validated",
    "Deferred"
)

foreach ($row in $tracker_rows)
{
    $fields = @($row.Trim('|').Split('|') | ForEach-Object { $_.Trim() })
    Assert-Condition ($fields.Count -eq 17) "Tracker row does not contain 17 fields: $($fields[0])"
    if ($fields[0] -match '^\[(Noise Generator|Non-Interactive Processes)\]')
    {
        Assert-Condition ($fields[2] -eq "Draft") "Unexpected UG-47 content state: $($fields[0])"
        Assert-Condition ($fields[4] -eq "Current") "Unexpected UG-47 source-review state: $($fields[0])"
        Assert-Condition ($fields[3] -in $allowed_media_states) "Invalid media state in: $($fields[0])"
        Assert-Condition ($fields[13] -eq "Not requested (UG-78)") `
            "Unexpected technical-review state: $($fields[0])"
        Assert-Condition ($fields[14] -eq "Not requested (UG-80)") `
            "Unexpected user-review state: $($fields[0])"
        continue
    }
    if ($fields[0] -match '^\[(Processes / Overview|Glossary)\]')
    {
        Assert-Condition ($fields[2] -eq "Draft") "Unexpected UG-55 content state: $($fields[0])"
        Assert-Condition ($fields[4] -eq "Current") "Unexpected UG-55 source-review state: $($fields[0])"
        $expected_media_state = if ($fields[0] -match '^\[Processes / Overview\]') { "Validated" } else { "Not needed" }
        Assert-Condition ($fields[3] -eq $expected_media_state) "Unexpected UG-55 media state: $($fields[0])"
        Assert-Condition ($fields[13] -eq "Not requested (UG-78)") `
            "Unexpected technical-review state: $($fields[0])"
        Assert-Condition ($fields[14] -eq "Not requested (UG-80)") `
            "Unexpected user-review state: $($fields[0])"
        continue
    }
    if ($fields[0] -match '^\[(Using Extensions|Extension Marketplace)\]')
    {
        Assert-Condition ($fields[2] -eq "Draft") "Unexpected UG-56 content state: $($fields[0])"
        Assert-Condition ($fields[4] -eq "Current") "Unexpected UG-56 source-review state: $($fields[0])"
        Assert-Condition ($fields[3] -eq "Planned") "Unexpected UG-56 media state: $($fields[0])"
        Assert-Condition ($fields[13] -eq "Not requested (UG-78)") `
            "Unexpected technical-review state: $($fields[0])"
        Assert-Condition ($fields[14] -eq "Not requested (UG-80)") `
            "Unexpected user-review state: $($fields[0])"
        continue
    }
    if ($fields[0] -match '^\[Keyboard Shortcuts\]')
    {
        Assert-Condition ($fields[2] -eq "Draft") "Unexpected UG-57 content state: $($fields[0])"
        Assert-Condition ($fields[4] -eq "Current") "Unexpected UG-57 source-review state: $($fields[0])"
        Assert-Condition ($fields[3] -eq "Not needed") "Unexpected UG-57 media state: $($fields[0])"
        Assert-Condition ($fields[13] -eq "Not requested (UG-78)") `
            "Unexpected technical-review state: $($fields[0])"
        Assert-Condition ($fields[14] -eq "Not requested (UG-80)") `
            "Unexpected user-review state: $($fields[0])"
        continue
    }
    if ($fields[0] -match '^\[(File Formats|System Requirements)\]')
    {
        Assert-Condition ($fields[2] -eq "Draft") "Unexpected UG-58 content state: $($fields[0])"
        Assert-Condition ($fields[4] -eq "Current") "Unexpected UG-58 source-review state: $($fields[0])"
        Assert-Condition ($fields[3] -eq "Not needed") "Unexpected UG-58 media state: $($fields[0])"
        Assert-Condition ($fields[13] -eq "Not requested (UG-78)") `
            "Unexpected technical-review state: $($fields[0])"
        Assert-Condition ($fields[14] -eq "Not requested (UG-80)") `
            "Unexpected user-review state: $($fields[0])"
        continue
    }
    if ($fields[0] -match '^\[Troubleshooting And Diagnostics\]')
    {
        Assert-Condition ($fields[2] -eq "Draft") "Unexpected UG-59 content state: $($fields[0])"
        Assert-Condition ($fields[4] -eq "Current") "Unexpected UG-59 source-review state: $($fields[0])"
        Assert-Condition ($fields[3] -eq "Not needed") "Unexpected UG-59 media state: $($fields[0])"
        Assert-Condition ($fields[13] -eq "Not requested (UG-78)") `
            "Unexpected technical-review state: $($fields[0])"
        Assert-Condition ($fields[14] -eq "Not requested (UG-80)") `
            "Unexpected user-review state: $($fields[0])"
        continue
    }
    if ($fields[0] -match '^\[Narrowband Imaging\]')
    {
        Assert-Condition ($fields[2] -eq "Draft") "Unexpected UG-61 content state: $($fields[0])"
        Assert-Condition ($fields[4] -eq "Current") "Unexpected UG-61 source-review state: $($fields[0])"
        Assert-Condition ($fields[3] -eq "Deferred") "Unexpected UG-61 media state: $($fields[0])"
        Assert-Condition ($fields[13] -eq "Not requested (UG-78)") `
            "Unexpected technical-review state: $($fields[0])"
        Assert-Condition ($fields[14] -eq "Not requested (UG-80)") `
            "Unexpected user-review state: $($fields[0])"
        continue
    }
    if ($fields[0] -match '^\[(Image Viewer|Process Windows|Docking Panels|Toolbar & Menus|Session Console|Image History|FITS Header, EXIF, And Astrometry Metadata|Masks)\]')
    {
        Assert-Condition ($fields[2] -eq "Draft") "Unexpected current component content state: $($fields[0])"
        Assert-Condition ($fields[4] -eq "Current") "Unexpected current component source-review state: $($fields[0])"
        Assert-Condition ($fields[3] -eq "Validated") "Unexpected current component media state: $($fields[0])"
        Assert-Condition ($fields[13] -eq "Not requested (UG-78)") `
            "Unexpected technical-review state: $($fields[0])"
        Assert-Condition ($fields[14] -eq "Not requested (UG-80)") `
            "Unexpected user-review state: $($fields[0])"
        continue
    }
    if ($fields[0] -match '^\[(Home|Installation|First Launch|Quick Start|Supported Formats|Interface / Overview|Dashboard|Auto Gradient Removal|DeepSNR|Settings|Astrometry Overlays|Annotate Image|Sequences|Saving And Exporting|Image Statistics|Histogram Analysis|Image Summary|Extract Sequence|File Formats|Image Integration|Image Integration Lite|Image Stacking|Image List Analyzer|Image Calibration|Image Registration|Plate Solving|Debayer|Spectrophotometric Color Calibration|Background Extraction|Cosmetic Correction|Split CFA|Merge CFA|CFA Integer Resample|Star Profiler v1 and v2|Color Calibration|Background Neutralization|Photometric Color Calibration|Linear Match|SCNR|Histogram Transformation|Generalized Hyperbolic Transformation|Curves Transformation|ArcSinh Stretch|Statistical Stretch|Gamma Correction|Exposure Correction|Wavelet Denoise|Total Variation Denoise|NL-Bayes|Patch PCA Denoise|Bilateral Filter|Gaussian Blur|Median Filter|GRAYCstoration|Banding Reduction|Unsharp Mask|Sharpen Alpha|High Pass Filter|Local Contrast Enhancement|Local Histogram Equalization|CLAHE|Star Mask|Starnet\+\+|StarNet\+\+ Star Mask|Star Reduction|Starmask Recombination|Profiled Starmask Recombination|Dark Structure Enhance|Channel Composition|LRGB Combination|NBRGB Combination|NB Color Mapper|Narrowband Palette Composer|Colorize|Crop|Resize|Rotate|Integer Resample|Image Compare|Image Blend|Range Selection|Mask Drawing|Binarize|Morphological Transformation|Multiscale Linear Transform|Adjust HSL|Adjust LCh|Adjust Brightness/Contrast|Color Saturation|Tighten Edges|Pixel Math|Broadband Imaging)\]')
    {
        Assert-Condition ($fields[2] -eq "Draft") "Unexpected completed content-phase state: $($fields[0])"
        $expected_source_review = if ($fields[0] -match '^\[(Home|Installation|First Launch|Quick Start|Supported Formats|Interface / Overview|Dashboard|DeepSNR|Settings|Astrometry Overlays|Annotate Image|Sequences|Saving And Exporting|Image Statistics|Histogram Analysis|Image Summary|Extract Sequence|File Formats|Image Integration|Image Integration Lite|Image Stacking|Image List Analyzer|Image Calibration|Image Registration|Plate Solving|Debayer|Spectrophotometric Color Calibration|Background Extraction|Cosmetic Correction|Split CFA|Merge CFA|CFA Integer Resample|Star Profiler v1 and v2|Color Calibration|Background Neutralization|Photometric Color Calibration|Linear Match|SCNR|Histogram Transformation|Generalized Hyperbolic Transformation|Curves Transformation|ArcSinh Stretch|Statistical Stretch|Gamma Correction|Exposure Correction|Wavelet Denoise|Total Variation Denoise|NL-Bayes|Patch PCA Denoise|Bilateral Filter|Gaussian Blur|Median Filter|GRAYCstoration|Banding Reduction|Unsharp Mask|Sharpen Alpha|High Pass Filter|Local Contrast Enhancement|Local Histogram Equalization|CLAHE|Star Mask|Starnet\+\+|StarNet\+\+ Star Mask|Star Reduction|Starmask Recombination|Profiled Starmask Recombination|Dark Structure Enhance|Channel Composition|LRGB Combination|NBRGB Combination|NB Color Mapper|Narrowband Palette Composer|Colorize|Crop|Resize|Rotate|Integer Resample|Image Compare|Image Blend|Range Selection|Mask Drawing|Binarize|Morphological Transformation|Multiscale Linear Transform|Adjust HSL|Adjust LCh|Adjust Brightness/Contrast|Color Saturation|Tighten Edges|Pixel Math|Broadband Imaging)\]') { "Current" } else { "Reviewed" }
        Assert-Condition ($fields[4] -eq $expected_source_review) "Unexpected completed source-review state: $($fields[0])"
    }
    else
    {
        Assert-Condition ($fields[2] -eq "Expansion required") "Unexpected UG-00 content state: $($fields[0])"
        Assert-Condition ($fields[4] -eq "Re-review required") "Unexpected source-review state: $($fields[0])"
    }
    Assert-Condition ($fields[3] -in $allowed_media_states) "Invalid media state in: $($fields[0])"
    Assert-Condition ($fields[13] -eq "Not requested (UG-78)") "Unexpected technical-review state: $($fields[0])"
    Assert-Condition ($fields[14] -eq "Not requested (UG-80)") "Unexpected user-review state: $($fields[0])"
}

$phase_rows = @($tracker_lines | Where-Object { $_ -match '^\| UG-\d{2} \|' })
$completed_phase_numbers = @(0..60) + @(65..68)
foreach ($phase_number in $completed_phase_numbers)
{
    $phase_id = "UG-{0:D2}" -f $phase_number
    $matching_rows = @($phase_rows | Where-Object { $_ -match "^\| $phase_id \|" })
    Assert-Condition ($matching_rows.Count -eq 1) "Expected one phase-ledger row for $phase_id; found $($matching_rows.Count)."

    $fields = @($matching_rows[0].Trim('|').Split('|') | ForEach-Object { $_.Trim() })
    Assert-Condition ($fields[1] -in @("Agent complete", "Complete", "Not applicable")) `
        "Unexpected completed phase-ledger status for ${phase_id}: $($fields[1])"
}

$deferred_phase_numbers = @(61..64)
foreach ($phase_number in $deferred_phase_numbers)
{
    $phase_id = "UG-{0:D2}" -f $phase_number
    $matching_rows = @($phase_rows | Where-Object { $_ -match "^\| $phase_id \|" })
    Assert-Condition ($matching_rows.Count -eq 1) "Expected one phase-ledger row for $phase_id; found $($matching_rows.Count)."

    $fields = @($matching_rows[0].Trim('|').Split('|') | ForEach-Object { $_.Trim() })
    Assert-Condition ($fields[1] -eq "Deferred") `
        "Unexpected deferred phase-ledger status for ${phase_id}: $($fields[1])"
}

$future_media_phase_numbers = @(69..73)
foreach ($phase_number in $future_media_phase_numbers)
{
    $phase_id = "UG-{0:D2}" -f $phase_number
    $matching_rows = @($phase_rows | Where-Object { $_ -match "^\| $phase_id \|" })
    Assert-Condition ($matching_rows.Count -eq 1) `
        "Expected one phase-ledger row for $phase_id; found $($matching_rows.Count)."
    $fields = @($matching_rows[0].Trim('|').Split('|') | ForEach-Object { $_.Trim() })
    Assert-Condition ($fields[1] -eq "Not started") `
        "Capture phase must remain Not started for ${phase_id}: $($fields[1])"
}

$template_paths = @(
    "docs/user-guide/templates/PROCESS_PAGE_TEMPLATE.md",
    "docs/user-guide/templates/COMPONENT_PAGE_TEMPLATE.md",
    "docs/user-guide/templates/WORKFLOW_PAGE_TEMPLATE.md",
    "docs/user-guide/templates/REFERENCE_PAGE_TEMPLATE.md"
)
# A released guide renders no authoring evidence: a page's owner, review, coverage, states and last strict build are
# recorded in its tracker row alone, and a template only sends its author there.
$evidence_patterns = @(
    '(?m)^## Page evidence\s*$',
    '(?m)^\| (Owner|Source-review date|Reviewed revision|Dirty reviewed sources|Media state|Last strict build) \|'
)
$common_rubric_terms = @(
    "prerequisites",
    "expected result",
    "reachability",
    "procedure",
    "inputs",
    "outputs",
    "warnings",
    "troubleshooting",
    "related",
    "references"
)

foreach ($template_path in $template_paths)
{
    $template_text = (Read-RepositoryFile $template_path) -join "`n"
    foreach ($pattern in $evidence_patterns)
    {
        $message = "Template $template_path still carries authoring evidence ($pattern)."
        Assert-Condition ($template_text -notmatch $pattern) $message
    }
    Assert-Condition ($template_text.Contains($tracker_path)) `
        "Template $template_path does not send its author to $tracker_path for the page's evidence."
    foreach ($term in $common_rubric_terms)
    {
        $message = "Template $template_path is missing rubric term '$term'."
        Assert-Condition ($template_text -match "(?i)$([regex]::Escape($term))") $message
    }
    Assert-Condition ($template_text.Contains("MEDIA_SLOT")) `
        "Template $template_path is missing canonical MEDIA_SLOT guidance."
    Assert-Condition (-not $template_text.Contains("SCREENSHOT_REQUEST")) `
        "Template $template_path still contains SCREENSHOT_REQUEST guidance."
}

$process_template = (Read-RepositoryFile $template_paths[0]) -join "`n"
$process_requirements = @(
    "## At a glance",
    "## Complete parameter and action reference",
    "Runtime default",
    "Persisted-default exception",
    "Valid values, step, or constraints",
    "Dependencies and enable/visibility rules",
    "Scope: preview/apply, channel, mask, still/sequence",
    "## How the algorithm works",
    "## Parameter interactions and starting points",
    "## Performance",
    "## Worked example"
)
foreach ($requirement in $process_requirements)
{
    $message = "Process template is missing section-3 requirement '$requirement'."
    Assert-Condition ($process_template.Contains($requirement)) $message
}

$prerequisite_path = "docs/user-guide/CONTENT_VERIFICATION_PREREQUISITES.md"
$prerequisite_lines = Read-RepositoryFile $prerequisite_path
$prerequisite_text = $prerequisite_lines -join "`n"
$required_dimensions = @(
    "Sample data",
    "Tools/models",
    "Catalogs/network",
    "Platform/GUI",
    "Owner",
    "Availability at UG-00",
    "Required evidence/gate"
)
foreach ($dimension in $required_dimensions)
{
    $message = "Prerequisite inventory is missing dimension '$dimension'."
    Assert-Condition ($prerequisite_text -match "\| $([regex]::Escape($dimension))") $message
}

$evidence_modes = @("S", "A", "E", "B", "G", "C", "P", "H-T", "H-U")
foreach ($mode in $evidence_modes)
{
    $message = "Prerequisite inventory is missing evidence mode '$mode'."
    Assert-Condition ($prerequisite_text -match "(?m)^\| $([regex]::Escape($mode)) \|") $message
}

$expected_phase_group_patterns = @(
    "UG-00",
    "UG-01",
    "UG-02",
    "UG-03",
    "UG-04.*UG-05",
    "UG-06",
    "UG-07.*UG-08",
    "UG-09",
    "UG-10",
    "UG-11.*UG-15",
    "UG-16",
    "UG-17",
    "UG-18",
    "UG-19.*UG-27",
    "UG-28",
    "UG-29",
    "UG-30.*UG-33",
    "UG-34.*UG-36",
    "UG-37.*UG-47",
    "UG-48.*UG-55",
    "UG-56",
    "UG-57.*UG-59",
    "UG-60.*UG-64\.x",
    "UG-65",
    "UG-66",
    "UG-67",
    "UG-68",
    "UG-69",
    "UG-70",
    "UG-71",
    "UG-72",
    "UG-73",
    "UG-74.*UG-75",
    "UG-76",
    "UG-77",
    "UG-78.*UG-79",
    "UG-80.*UG-81",
    "UG-82",
    "UG-83"
)
foreach ($phase_group_pattern in $expected_phase_group_patterns)
{
    $message = "Prerequisite inventory is missing phase group matching '$phase_group_pattern'."
    Assert-Condition ($prerequisite_text -match "(?m)^\| $phase_group_pattern \|") $message
}

$page_root = Join-Path $repo_root "docs/user-guide/docs/en"
$page_files = @(Get-ChildItem -LiteralPath $page_root -Recurse -File -Filter "*.md")
Assert-Condition ($page_files.Count -gt 0) "No user-guide page found under docs/user-guide/docs/en."
foreach ($page_file in $page_files)
{
    $page_text = [System.IO.File]::ReadAllText($page_file.FullName)
    $page_path = $page_file.FullName.Substring($repo_root.Length + 1).Replace('\', '/')
    foreach ($pattern in $evidence_patterns)
    {
        $message = "Page $page_path renders authoring evidence ($pattern); record it in $tracker_path alone."
        Assert-Condition ($page_text -notmatch $pattern) $message
    }
}

$foundation_paths = @(
    "build/scripts/validate_ug00_user_guide_foundation.ps1",
    "docs/dev/USER_GUIDE_AUTHORING.md",
    $tracker_path,
    $prerequisite_path
) + $template_paths
foreach ($foundation_path in $foundation_paths)
{
    $line_number = 0
    foreach ($line in (Read-RepositoryFile $foundation_path))
    {
        ++$line_number
        $message = "Trailing whitespace in ${foundation_path}:$line_number."
        Assert-Condition ($line -notmatch '[ \t]+$') $message
    }
}

Write-Output ("UG-00 foundation validation PASS: 113 tracker rows/17 fields; " +
    "$($completed_phase_numbers.Count) completed phase-ledger rows; $($deferred_phase_numbers.Count) deferred phase-ledger rows; $($future_media_phase_numbers.Count) not-started media phase rows; 4 templates; $($page_files.Count) pages without authoring evidence; $($expected_phase_group_patterns.Count) prerequisite groups; " +
    "foundation whitespace clean.")
