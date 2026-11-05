param(
    [switch]$RunFixtures
)

$ErrorActionPreference = "Stop"
$script:repository_root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

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

function Read-Text
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    Assert-Condition (Test-Path -LiteralPath $Path -PathType Leaf) "Missing required file: $Path"
    return [System.IO.File]::ReadAllText($Path)
}

function Write-Utf8NoBom
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Text
    )

    [System.IO.File]::WriteAllText($Path, $Text, [System.Text.UTF8Encoding]::new($false))
}

function Get-RepositoryRelativePath
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepositoryRoot,

        [Parameter(Mandatory = $true)]
        [string]$FullPath
    )

    $resolved_root = [System.IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\', '/')
    $resolved_path = [System.IO.Path]::GetFullPath($FullPath)
    Assert-Condition ($resolved_path.StartsWith($resolved_root + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase)) `
        "Path is outside the repository root: $resolved_path"
    return $resolved_path.Substring($resolved_root.Length + 1).Replace('\', '/')
}

function Remove-CodeTicks
{
    param([string]$Value)

    $trimmed = $Value.Trim()
    if ($trimmed.Length -ge 2 -and $trimmed[0] -eq '`' -and $trimmed[$trimmed.Length - 1] -eq '`')
    {
        return $trimmed.Substring(1, $trimmed.Length - 2)
    }
    return $trimmed
}

function Get-ManifestRows
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$ManifestPath
    )

    $rows = [System.Collections.Generic.List[object]]::new()
    foreach ($line in [System.IO.File]::ReadAllLines($ManifestPath))
    {
        if ($line -notmatch '^\| UG-MEDIA-[0-9]{3} \|')
        {
            continue
        }

        $fields = @($line.Trim('|').Split('|') | ForEach-Object { $_.Trim() })
        Assert-Condition ($fields.Count -eq 12) `
            "Canonical manifest row must contain 12 fields: $($fields[0])"

        $consumers = @([regex]::Matches($fields[3], '`([^`]+)`') |
                ForEach-Object { $_.Groups[1].Value.Replace('\', '/') })
        $rows.Add([pscustomobject]@{
                Id = $fields[0]
                Phase = $fields[1]
                Asset = (Remove-CodeTicks $fields[2]).Replace('\', '/')
                Consumers = $consumers
                Role = $fields[4]
                Disposition = $fields[5]
                ReaderQuestion = $fields[6]
                RequiredState = $fields[7]
                EvidenceGate = $fields[8]
                PrivacyBoundary = $fields[9]
                Status = $fields[10]
                ReplacementTrigger = $fields[11]
            })
    }
    return @($rows)
}

function Get-TriageRows
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$TriagePath
    )

    $rows = [System.Collections.Generic.List[object]]::new()
    $phase = $null
    foreach ($line in [System.IO.File]::ReadAllLines($TriagePath))
    {
        if ($line -match '^### (UG-[0-9]{2}) ')
        {
            $phase = $Matches[1]
            continue
        }
        if ($line -notmatch '^\| ([0-9]{3}) \|')
        {
            continue
        }

        $fields = @($line.Trim('|').Split('|') | ForEach-Object { $_.Trim() })
        Assert-Condition ($fields.Count -eq 5) "Triage decision row must contain five fields: $line"
        $rows.Add([pscustomobject]@{
                Row = [int]$fields[0]
                Target = (Remove-CodeTicks $fields[1]).Replace('\', '/')
                Decision = $fields[2]
                Phase = $phase
            })
    }
    return @($rows)
}

function Get-MarkdownInventory
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepositoryRoot
    )

    $guide_root = Join-Path $RepositoryRoot 'docs/user-guide/docs/en'
    $pages = @{}
    $slots = [System.Collections.Generic.List[object]]::new()
    $links = [System.Collections.Generic.List[object]]::new()

    foreach ($file in Get-ChildItem -LiteralPath $guide_root -Recurse -Filter '*.md')
    {
        $page = Get-RepositoryRelativePath $RepositoryRoot $file.FullName
        $text = [System.IO.File]::ReadAllText($file.FullName)
        $pages[$page] = $text

        Assert-Condition (-not [regex]::IsMatch($text, '<!--\s*SCREENSHOT_REQUEST\b',
                [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)) `
            "SCREENSHOT_REQUEST comment remains in ${page}; use a MEDIA_SLOT ID."
        Assert-Condition (-not [regex]::IsMatch($text,
                '<!--[^>]*(?:Missing media remains|Screenshot requests remain)[^>]*-->',
                [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)) `
            "Generic missing-media comment remains in $page."
        Assert-Condition (-not $text.Contains('assets/images/interface/dashboard.png')) `
            "Stale dashboard screenshot target remains in $page."

        $all_slot_comments = @([regex]::Matches($text, '<!--[^>]*MEDIA_SLOT[^>]*-->'))
        $valid_slot_comments = @([regex]::Matches($text,
                '<!-- MEDIA_SLOT: (UG-MEDIA-[0-9]{3}) -->'))
        Assert-Condition ($all_slot_comments.Count -eq $valid_slot_comments.Count) `
            "Malformed MEDIA_SLOT comment in $page."

        foreach ($match in $valid_slot_comments)
        {
            $slots.Add([pscustomobject]@{ Id = $match.Groups[1].Value; Page = $page })
        }

        foreach ($match in [regex]::Matches($text, '!\[[^\]]*\]\((?<target><[^>]+>|[^)\s]+)'))
        {
            $target = $match.Groups['target'].Value.Trim('<', '>')
            if ($target -match '^(?:[a-z]+:|#)')
            {
                continue
            }
            $target = [System.Uri]::UnescapeDataString(($target -replace '#.*$', ''))
            $full_target = [System.IO.Path]::GetFullPath((Join-Path $file.DirectoryName $target))
            $relative_target = Get-RepositoryRelativePath $RepositoryRoot $full_target
            if ($relative_target -notmatch '^docs/user-guide/docs/en/assets/images/')
            {
                continue
            }
            $links.Add([pscustomobject]@{
                    Page = $page
                    Asset = $relative_target
                    Exists = Test-Path -LiteralPath $full_target -PathType Leaf
                })
        }
    }

    return [pscustomobject]@{ Pages = $pages; Slots = @($slots); Links = @($links) }
}

function Invoke-MediaValidation
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepositoryRoot
    )

    $manifest_path = Join-Path $RepositoryRoot 'docs/user-guide/SCREENSHOT_REQUEST_MANIFEST.md'
    $contract_path = Join-Path $RepositoryRoot 'docs/user-guide/audits/user-guide-media-contract.json'
    $triage_path = Join-Path $RepositoryRoot 'docs/dev/USER_GUIDE_SCREENSHOT_TRIAGE.md'
    $tracker_path = Join-Path $RepositoryRoot 'docs/user-guide/DOCUMENTATION_COMPLETION_TRACKER.md'
    $provenance_path = Join-Path $RepositoryRoot 'docs/user-guide/docs/en/assets/MEDIA_PROVENANCE.md'

    $contract = (Read-Text $contract_path) | ConvertFrom-Json
    $manifest_text = Read-Text $manifest_path
    Assert-Condition ($manifest_text.Contains(
            '| Media ID | Phase | Canonical asset | Consumer pages | Role | Disposition |')) `
        'The manifest does not use the canonical media schema.'
    Assert-Condition (-not $manifest_text.Contains('| Target file | Description | Priority | Status |')) `
        'The manifest still uses the Target file/Priority screenshot schema.'
    Assert-Condition (-not [regex]::IsMatch($manifest_text, '(?m)^\|[^\r\n]*\bPriority\b')) `
        'Priority remains in the canonical media manifest.'
    Assert-Condition (-not $manifest_text.Contains('assets/images/interface/dashboard.png')) `
        'Stale dashboard screenshot target remains in the canonical manifest.'

    $manifest_rows = @(Get-ManifestRows $manifest_path)
    Assert-Condition ($manifest_rows.Count -eq [int]$contract.expectedCanonicalRows) `
        "Expected $($contract.expectedCanonicalRows) canonical rows; found $($manifest_rows.Count)."

    $allowed_roles = @('Orientation', 'State', 'Interaction', 'Comparison', 'Verified result')
    $allowed_statuses = @('Planned', 'Ready', 'Blocked', 'Captured', 'Placed', 'Validated')
    $manifest_by_id = @{}
    $manifest_by_asset = @{}
    foreach ($row in $manifest_rows)
    {
        Assert-Condition (-not $manifest_by_id.ContainsKey($row.Id)) "Duplicate Media ID: $($row.Id)."
        Assert-Condition (-not $manifest_by_asset.ContainsKey($row.Asset)) `
            "Duplicate canonical asset path: $($row.Asset)."
        Assert-Condition ($row.Phase -match '^UG-(6[6-9]|7[0-3])$') `
            "Invalid phase owner for $($row.Id): $($row.Phase)."
        Assert-Condition ($row.Asset -match '^docs/user-guide/docs/en/assets/images/.+\.png$') `
            "Canonical asset is not a repository-relative PNG path for $($row.Id)."
        Assert-Condition ($row.Role -in $allowed_roles) "Invalid editorial role for $($row.Id)."
        Assert-Condition ($row.Disposition -in @('Keep', 'Deferred')) `
            "Invalid retained disposition for $($row.Id)."
        Assert-Condition ($row.Status -in $allowed_statuses) "Invalid capture status for $($row.Id)."
        Assert-Condition ($row.ReaderQuestion.Length -gt 0 -and $row.RequiredState.Length -gt 0 -and
            $row.EvidenceGate.Length -gt 0 -and $row.PrivacyBoundary.Length -gt 0 -and
            $row.ReplacementTrigger.Length -gt 0) "Incomplete canonical brief for $($row.Id)."
        Assert-Condition ($row.Consumers.Count -gt 0 -or $row.Disposition -ne 'Keep') `
            "Kept row has no consumer: $($row.Id)."
        if ($row.Disposition -eq 'Deferred')
        {
            Assert-Condition ($row.Status -eq 'Blocked') `
                "Deferred row must remain Blocked: $($row.Id)."
        }
        $manifest_by_id[$row.Id] = $row
        $manifest_by_asset[$row.Asset] = $row
    }

    $triage_rows = @(Get-TriageRows $triage_path)
    Assert-Condition ($triage_rows.Count -eq [int]$contract.expectedOriginalRows) `
        "Expected $($contract.expectedOriginalRows) triage rows; found $($triage_rows.Count)."
    Assert-Condition (@($triage_rows | Group-Object Row | Where-Object Count -ne 1).Count -eq 0) `
        'The triage row numbers are not unique.'
    $triage_counts = @{}
    foreach ($decision in @('Keep', 'Merge', 'Not needed', 'Deferred'))
    {
        $triage_counts[$decision] = @($triage_rows | Where-Object Decision -eq $decision).Count
    }
    Assert-Condition ($triage_counts['Keep'] -eq [int]$contract.expectedKeep) 'Triage Keep total mismatch.'
    Assert-Condition ($triage_counts['Merge'] -eq [int]$contract.expectedMergedSources) `
        'Triage Merge total mismatch.'
    Assert-Condition ($triage_counts['Not needed'] -eq [int]$contract.expectedNotNeeded) `
        'Triage Not needed total mismatch.'
    Assert-Condition ($triage_counts['Deferred'] -eq [int]$contract.expectedDeferred) `
        'Triage Deferred total mismatch.'

    $fixture_not_needed = @($contract.notNeededRows | ForEach-Object { [int]$_ } | Sort-Object)
    $triage_not_needed = @($triage_rows | Where-Object Decision -eq 'Not needed' |
            ForEach-Object Row | Sort-Object)
    Assert-Condition (($fixture_not_needed -join ',') -eq ($triage_not_needed -join ',')) `
        'The deterministic Not needed fixture does not match the triage.'
    $fixture_deferred = @($contract.deferredRows | ForEach-Object { [int]$_ } | Sort-Object)
    $triage_deferred = @($triage_rows | Where-Object Decision -eq 'Deferred' |
            ForEach-Object Row | Sort-Object)
    Assert-Condition (($fixture_deferred -join ',') -eq ($triage_deferred -join ',')) `
        'The deterministic Deferred fixture does not match the triage.'

    $expected_manifest_ids = @($triage_rows | Where-Object { $_.Decision -in @('Keep', 'Deferred') } |
            ForEach-Object { 'UG-MEDIA-{0:D3}' -f $_.Row } | Sort-Object)
    $actual_manifest_ids = @($manifest_rows.Id | Sort-Object)
    Assert-Condition (($expected_manifest_ids -join ',') -eq ($actual_manifest_ids -join ',')) `
        'Canonical manifest IDs do not match the retained triage rows.'

    $inventory = Get-MarkdownInventory $RepositoryRoot
    $slot_groups = @{}
    foreach ($slot in $inventory.Slots)
    {
        Assert-Condition ($manifest_by_id.ContainsKey($slot.Id)) `
            "MEDIA_SLOT $($slot.Id) in $($slot.Page) does not resolve to one manifest row."
        $row = $manifest_by_id[$slot.Id]
        Assert-Condition ($row.Disposition -eq 'Keep') `
            "Deferred media has a release-page slot: $($slot.Id) in $($slot.Page)."
        Assert-Condition ($slot.Page -in $row.Consumers) `
            "MEDIA_SLOT consumer is not declared by $($slot.Id): $($slot.Page)."
        $key = "$($slot.Id)|$($slot.Page)"
        if (-not $slot_groups.ContainsKey($key))
        {
            $slot_groups[$key] = 0
        }
        $slot_groups[$key]++
        Assert-Condition ($slot_groups[$key] -eq 1) `
            "Duplicate MEDIA_SLOT $($slot.Id) in $($slot.Page)."
    }

    $link_groups = @{}
    foreach ($link in $inventory.Links)
    {
        $key = "$($link.Asset)|$($link.Page)"
        if (-not $link_groups.ContainsKey($key))
        {
            $link_groups[$key] = 0
        }
        $link_groups[$key]++
        if ($manifest_by_asset.ContainsKey($link.Asset))
        {
            $linked_row = $manifest_by_asset[$link.Asset]
            Assert-Condition ($link.Page -in $linked_row.Consumers) `
                "Linked canonical asset has undeclared consumer: $($link.Asset) in $($link.Page)."
            Assert-Condition ($linked_row.Status -in @('Placed', 'Validated')) `
                "Missing asset is linked: $($link.Asset) in $($link.Page)."
        }
        Assert-Condition $link.Exists `
            "Linked image does not exist: $($link.Asset) from $($link.Page)."
    }

    foreach ($row in $manifest_rows)
    {
        foreach ($consumer in $row.Consumers)
        {
            Assert-Condition ($inventory.Pages.ContainsKey($consumer)) `
                "Consumer page does not exist for $($row.Id): $consumer."
            $slot_key = "$($row.Id)|$consumer"
            $link_key = "$($row.Asset)|$consumer"
            if ($row.Disposition -eq 'Deferred')
            {
                Assert-Condition (-not $slot_groups.ContainsKey($slot_key)) `
                    "Deferred media has a release-page slot: $($row.Id) in $consumer."
                Assert-Condition (-not $link_groups.ContainsKey($link_key)) `
                    "Deferred media is linked as release evidence: $($row.Id) in $consumer."
                continue
            }

            if ($row.Status -in @('Planned', 'Ready', 'Blocked'))
            {
                Assert-Condition (-not $link_groups.ContainsKey($link_key)) `
                    "Missing asset is linked: $($row.Asset) in $consumer."
                Assert-Condition ($slot_groups.ContainsKey($slot_key)) `
                    "Missing canonical slot for $($row.Id) consumer $consumer."
            }
            elseif ($row.Status -in @('Placed', 'Validated'))
            {
                Assert-Condition (-not $slot_groups.ContainsKey($slot_key)) `
                    "Placed media still has a slot: $($row.Id) in $consumer."
                Assert-Condition ($link_groups.ContainsKey($link_key) -and $link_groups[$link_key] -eq 1) `
                    "Placed media is not linked exactly once by ${consumer}: $($row.Id)."
            }
        }
    }

    foreach ($merge in @($contract.mergedSources))
    {
        $original_row = [int]$merge.originalRow
        $source_row = @($triage_rows | Where-Object { $_.Row -eq $original_row })
        Assert-Condition ($source_row.Count -eq 1 -and $source_row[0].Decision -eq 'Merge') `
            "Merged fixture row does not match triage row $($merge.originalRow)."
        Assert-Condition ($source_row[0].Target -eq [string]$merge.sourceAsset) `
            "Merged fixture source path mismatch for row $($merge.originalRow)."
        Assert-Condition ($manifest_by_id.ContainsKey([string]$merge.canonicalMediaId)) `
            "Merged canonical Media ID is missing: $($merge.canonicalMediaId)."
        $canonical = $manifest_by_id[[string]$merge.canonicalMediaId]
        Assert-Condition ($canonical.Disposition -eq 'Keep') `
            "Merged source does not resolve to a kept asset: $($merge.sourceAsset)."
        Assert-Condition (($canonical.Asset -replace '^docs/user-guide/docs/en/', '') -eq
            [string]$merge.canonicalAsset) "Merged canonical asset mismatch: $($merge.sourceAsset)."
        Assert-Condition (-not $manifest_by_asset.ContainsKey(
                "docs/user-guide/docs/en/$($merge.sourceAsset)")) `
            "Merged source still has a canonical manifest row: $($merge.sourceAsset)."
        $merged_source_path = Join-Path $RepositoryRoot `
            "docs/user-guide/docs/en/$($merge.sourceAsset)"
        Assert-Condition (-not (Test-Path -LiteralPath $merged_source_path)) `
            "Merged source alias file exists: $($merge.sourceAsset)."
        $merged_links = @($inventory.Links | Where-Object {
                $_.Asset -eq "docs/user-guide/docs/en/$($merge.sourceAsset)" })
        Assert-Condition ($merged_links.Count -eq 0) `
            "Merged source alias is linked: $($merge.sourceAsset)."
        foreach ($consumer in @($merge.requiredConsumers))
        {
            Assert-Condition ([string]$consumer -in $canonical.Consumers) `
                "Merged consumer is missing from $($canonical.Id): $consumer."
        }
    }

    foreach ($not_needed in @($triage_rows | Where-Object Decision -eq 'Not needed'))
    {
        $id = 'UG-MEDIA-{0:D3}' -f $not_needed.Row
        Assert-Condition (-not $manifest_by_id.ContainsKey($id)) `
            "Not needed item remains in the manifest: $id."
        $target = "docs/user-guide/docs/en/$($not_needed.Target)"
        Assert-Condition (-not @($inventory.Links | Where-Object Asset -eq $target).Count) `
            "Not needed asset is linked: $($not_needed.Target)."
    }

    $provenance_rows = @{}
    foreach ($line in [System.IO.File]::ReadAllLines($provenance_path))
    {
        if ($line -notmatch '^\| UG-MEDIA-[0-9]{3} \|')
        {
            continue
        }
        $fields = @($line.Trim('|').Split('|') | ForEach-Object { $_.Trim() })
        Assert-Condition ($fields.Count -eq 8) "Media provenance row must contain eight fields."
        $id = $fields[0]
        Assert-Condition (-not $provenance_rows.ContainsKey($id)) "Duplicate provenance row for $id."
        $provenance_rows[$id] = [pscustomobject]@{
            Asset = Remove-CodeTicks $fields[1]
            Hash = Remove-CodeTicks $fields[5]
        }
    }
    foreach ($id in $provenance_rows.Keys)
    {
        Assert-Condition ($manifest_by_id.ContainsKey($id)) "Provenance references unknown Media ID $id."
    }
    foreach ($row in @($manifest_rows | Where-Object Status -in @('Captured', 'Placed', 'Validated')))
    {
        $asset_path = Join-Path $RepositoryRoot $row.Asset
        Assert-Condition (Test-Path -LiteralPath $asset_path -PathType Leaf) `
            "Captured/placed asset does not exist: $($row.Asset)."
        Assert-Condition ($provenance_rows.ContainsKey($row.Id)) `
            "Captured/placed media $($row.Id) has no provenance row."
        $provenance = $provenance_rows[$row.Id]
        Assert-Condition ($provenance.Asset -eq ($row.Asset -replace '^docs/user-guide/docs/en/', '')) `
            "Provenance asset mismatch for $($row.Id)."
        Assert-Condition ($provenance.Hash -match '^[0-9a-fA-F]{64}$') `
            "Provenance hash is missing for $($row.Id)."
        $actual_hash = (Get-FileHash -LiteralPath $asset_path -Algorithm SHA256).Hash.ToLowerInvariant()
        Assert-Condition ($actual_hash -eq $provenance.Hash.ToLowerInvariant()) `
            "Provenance hash mismatch for $($row.Id)."
    }

    # The contract states how many kept, deferred and still-missing assets each capture phase owns; the manifest's
    # rows must hold exactly those counts.
    $capture_phases = @(66..73 | ForEach-Object { 'UG-{0:D2}' -f $_ })
    foreach ($ownership in @('phaseOwnership', 'phaseDeferred', 'phaseFutureStandaloneFiles'))
    {
        $declared_phases = @($contract.$ownership.PSObject.Properties.Name | Sort-Object)
        Assert-Condition (($declared_phases -join ',') -eq ($capture_phases -join ',')) `
            "The contract's $ownership must name the eight capture phases UG-66 through UG-73."
    }
    foreach ($phase in $capture_phases)
    {
        $manifest_keep = @($manifest_rows | Where-Object {
                $_.Phase -eq $phase -and $_.Disposition -eq 'Keep' }).Count
        $manifest_deferred = @($manifest_rows | Where-Object {
                $_.Phase -eq $phase -and $_.Disposition -eq 'Deferred' }).Count
        $manifest_future = @($manifest_rows | Where-Object {
                $_.Phase -eq $phase -and $_.Disposition -eq 'Keep' -and
                $_.Status -notin @('Captured', 'Placed', 'Validated') }).Count
        Assert-Condition ($manifest_keep -eq [int]$contract.phaseOwnership.$phase) `
            "Phase ownership mismatch for $phase Keep."
        Assert-Condition ($manifest_deferred -eq [int]$contract.phaseDeferred.$phase) `
            "Phase ownership mismatch for $phase Deferred."
        Assert-Condition ($manifest_future -eq [int]$contract.phaseFutureStandaloneFiles.$phase) `
            "Phase ownership mismatch for $phase future files."
    }

    Assert-Condition ($manifest_by_id['UG-MEDIA-103'].Phase -eq 'UG-70') `
        'Multiscale Linear Transform must be owned by UG-70.'
    Assert-Condition ($manifest_by_id['UG-MEDIA-100'].Phase -eq 'UG-71' -and
        $manifest_by_id['UG-MEDIA-102'].Phase -eq 'UG-71') `
        'Mask Drawing and Morphological Transformation must be owned by UG-71.'
    $binarize = @($triage_rows | Where-Object Row -eq 101)
    Assert-Condition ($binarize.Count -eq 1 -and $binarize[0].Phase -eq 'UG-71' -and
        $binarize[0].Decision -eq 'Not needed') `
        'Binarize triage ownership must be UG-71 with a Not needed disposition.'

    $future_count = @($manifest_rows | Where-Object {
            $_.Disposition -eq 'Keep' -and $_.Status -notin @('Captured', 'Placed', 'Validated') }).Count
    Assert-Condition ($future_count -eq [int]$contract.expectedFutureStandaloneCaptures) `
        "Expected $($contract.expectedFutureStandaloneCaptures) future standalone captures; found $future_count."
    Assert-Condition (@($manifest_rows | Where-Object Disposition -eq 'Deferred').Count -eq
        [int]$contract.expectedDeferred) 'Deferred media was included in the release capture count.'

    $allowed_page_states = @('Not assessed', 'Not needed', 'Planned', 'Captured', 'Placed',
        'Validated', 'Deferred')
    $tracker_states = @{}
    foreach ($line in [System.IO.File]::ReadAllLines($tracker_path))
    {
        if ($line -notmatch '^\| \[')
        {
            continue
        }
        $fields = @($line.Trim('|').Split('|') | ForEach-Object { $_.Trim() })
        Assert-Condition ($fields.Count -eq 17) 'Tracker page row must contain 17 fields.'
        Assert-Condition ($fields[0] -match '\((docs/en/[^)]+)\)') `
            "Tracker page path is malformed: $($fields[0])."
        $page = "docs/user-guide/$($Matches[1])"
        Assert-Condition ($fields[3] -in $allowed_page_states) `
            "Invalid tracker media state for ${page}: $($fields[3])."
        $tracker_states[$page] = $fields[3]
    }
    foreach ($slot in $inventory.Slots)
    {
        Assert-Condition ($tracker_states[$slot.Page] -eq 'Planned') `
            "Tracker media state is not Planned for slot consumer $($slot.Page)."
    }
    foreach ($row in @($manifest_rows | Where-Object Disposition -eq 'Deferred'))
    {
        foreach ($consumer in $row.Consumers)
        {
            Assert-Condition ($tracker_states[$consumer] -eq 'Deferred') `
                "Tracker media state is not Deferred for $consumer."
        }
    }
    foreach ($consumer in $manifest_by_id['UG-MEDIA-001'].Consumers)
    {
        if (-not @($inventory.Slots | Where-Object Page -eq $consumer).Count)
        {
            Assert-Condition ($tracker_states[$consumer] -in @('Placed', 'Validated')) `
                "Tracker media state is not Placed or Validated for hero consumer $consumer."
        }
    }
    foreach ($phase in 66..73)
    {
        $phase_id = 'UG-{0:D2}' -f $phase
        $phase_rows = @($manifest_rows | Where-Object {
                $_.Phase -eq $phase_id -and $_.Disposition -eq 'Keep' })
        $all_validated = $phase_rows.Count -gt 0 -and
            @($phase_rows | Where-Object Status -ne 'Validated').Count -eq 0
        $has_progress = @($phase_rows | Where-Object {
                $_.Status -in @('Ready', 'Captured', 'Placed', 'Validated') }).Count -gt 0
        $expected_status = if ($all_validated)
        {
            'Agent complete'
        }
        elseif ($has_progress)
        {
            'In progress'
        }
        else
        {
            'Not started'
        }
        Assert-Condition ([regex]::IsMatch((Read-Text $tracker_path),
                "(?m)^\| $phase_id \| $expected_status \|")) `
            "Tracker capture phase is not ${expected_status}: $phase_id."
    }

    return [pscustomobject]@{
        CanonicalRows = $manifest_rows.Count
        Keep = @($manifest_rows | Where-Object Disposition -eq 'Keep').Count
        Deferred = @($manifest_rows | Where-Object Disposition -eq 'Deferred').Count
        Slots = $inventory.Slots.Count
        Future = $future_count
    }
}

function Invoke-NegativeFixtures
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepositoryRoot
    )

    $contract_path = Join-Path $RepositoryRoot `
        'docs/user-guide/audits/user-guide-media-contract.json'
    $contract = (Read-Text $contract_path) | ConvertFrom-Json
    $temporary_root = Join-Path ([System.IO.Path]::GetTempPath()) `
        ('afternight-media-fixtures-' + [System.Guid]::NewGuid().ToString('N'))
    [System.IO.Directory]::CreateDirectory($temporary_root) | Out-Null
    try
    {
        Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'docs') -Destination $temporary_root -Recurse

        foreach ($fixture in @($contract.negativeFixtures))
        {
            $target = $null
            switch ([string]$fixture.name)
            {
                'unknown-slot'
                {
                    $target = Join-Path $temporary_root 'docs/user-guide/docs/en/getting-started/installation.md'
                    $mutated = (Read-Text $target) + "`n<!-- MEDIA_SLOT: UG-MEDIA-999 -->`n"
                }
                'duplicate-media-id'
                {
                    $target = Join-Path $temporary_root 'docs/user-guide/SCREENSHOT_REQUEST_MANIFEST.md'
                    $mutated = [regex]::Replace((Read-Text $target),
                        '(?m)^\| UG-MEDIA-002 \|', '| UG-MEDIA-001 |', 1)
                }
                'linked-missing-asset'
                {
                    $target = Join-Path $temporary_root 'docs/user-guide/SCREENSHOT_REQUEST_MANIFEST.md'
                    $mutated = [regex]::Replace((Read-Text $target),
                        '(?m)^(\| UG-MEDIA-002 \|.*\| )Validated( \|.*)$', '$1Planned$2', 1)
                }
                'request-comment'
                {
                    $target = Join-Path $temporary_root 'docs/user-guide/docs/en/getting-started/installation.md'
                    $mutated = (Read-Text $target) + "`n<!-- SCREENSHOT_REQUEST: dashboard -->`n"
                }
                'stale-target'
                {
                    $target = Join-Path $temporary_root 'docs/user-guide/docs/en/getting-started/installation.md'
                    $mutated = (Read-Text $target) + "`n<!-- assets/images/interface/dashboard.png -->`n"
                }
                'phase-mismatch'
                {
                    # The contract declares a fourth kept asset for UG-70, which owns three in the manifest.
                    $target = Join-Path $temporary_root 'docs/user-guide/audits/user-guide-media-contract.json'
                    $mutated = [regex]::new('("phaseOwnership"\s*:\s*\{[^}]*?"UG-70"\s*:\s*)3\b').Replace(
                        (Read-Text $target), '${1}4', 1)
                }
                'deferred-slot'
                {
                    $target = Join-Path $temporary_root `
                        'docs/user-guide/docs/en/processes/color-composition/nbrgb-combination.md'
                    $mutated = (Read-Text $target) + "`n<!-- MEDIA_SLOT: UG-MEDIA-088 -->`n"
                }
                'missing-provenance'
                {
                    $target = Join-Path $temporary_root `
                        'docs/user-guide/docs/en/assets/MEDIA_PROVENANCE.md'
                    $mutated = (([regex]::Split((Read-Text $target), "`n") |
                                Where-Object { $_ -notmatch '^\| UG-MEDIA-001 \|' }) -join "`n")
                }
                default
                {
                    throw "Unknown media validation fixture: $($fixture.name)."
                }
            }

            $original = Read-Text $target
            try
            {
                Write-Utf8NoBom $target $mutated
                $failed_as_expected = $false
                try
                {
                    Invoke-MediaValidation $temporary_root | Out-Null
                }
                catch
                {
                    Assert-Condition ($_.Exception.Message.Contains([string]$fixture.expectedFailure)) `
                        "Fixture $($fixture.name) failed for the wrong reason: $($_.Exception.Message)"
                    $failed_as_expected = $true
                }
                Assert-Condition $failed_as_expected "Fixture $($fixture.name) unexpectedly passed."
            }
            finally
            {
                Write-Utf8NoBom $target $original
            }
        }
    }
    finally
    {
        $resolved_temp = [System.IO.Path]::GetFullPath($temporary_root)
        Assert-Condition ($resolved_temp.StartsWith([System.IO.Path]::GetTempPath(),
                [System.StringComparison]::OrdinalIgnoreCase)) `
            "Refusing to remove fixture directory outside the system temp root: $resolved_temp"
        Remove-Item -LiteralPath $resolved_temp -Recurse -Force
    }

    return @($contract.negativeFixtures).Count
}

$result = Invoke-MediaValidation $script:repository_root
Write-Output (("User-guide media validation PASS: {0} canonical rows; {1} Keep; " +
        "{2} Deferred; {3} MEDIA_SLOT placements; {4} future standalone captures.") -f
    $result.CanonicalRows, $result.Keep, $result.Deferred, $result.Slots, $result.Future)

if ($RunFixtures)
{
    $fixture_count = Invoke-NegativeFixtures $script:repository_root
    Write-Output "User-guide media negative fixtures PASS: $fixture_count expected failures."
}
