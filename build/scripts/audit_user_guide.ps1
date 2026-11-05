param(
    [string]$RepositoryRoot,
    [string]$OutputPath,
    [switch]$CheckBaseline,
    [switch]$FailOnFindings
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepositoryRoot))
{
    $RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}
else
{
    $RepositoryRoot = (Resolve-Path $RepositoryRoot).Path
}

$config_path = Join-Path $RepositoryRoot "docs/user-guide/audits/user-guide-audit-config.json"
$baseline_path = Join-Path $RepositoryRoot "docs/user-guide/audits/user-guide-audit-baseline.json"
$docs_root = Join-Path $RepositoryRoot "docs/user-guide/docs/en"
$mkdocs_path = Join-Path $RepositoryRoot "docs/user-guide/mkdocs.yml"
$tracker_path = Join-Path $RepositoryRoot "docs/user-guide/DOCUMENTATION_COMPLETION_TRACKER.md"
$registry_path = Join-Path $RepositoryRoot "src/AfterNightProcesses/app/ProcessRegistryBuiltins.cpp"
$ui_builder_path = Join-Path $RepositoryRoot "src/AfterNightControls/app/AfterNightAppUiBuilder.h"

function Read-Text
{
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf))
    {
        throw "Missing audit input: $Path"
    }
    return [System.IO.File]::ReadAllText($Path)
}

function Write-Utf8NoBom
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Text
    )

    $parent = Split-Path -Parent $Path
    if (-not [string]::IsNullOrEmpty($parent))
    {
        [System.IO.Directory]::CreateDirectory($parent) | Out-Null
    }
    [System.IO.File]::WriteAllText($Path, $Text, [System.Text.UTF8Encoding]::new($false))
}

function Normalize-Title
{
    param([AllowEmptyString()][string]$Value)

    return (($Value.ToLowerInvariant() -replace '[^a-z0-9+]', ''))
}

function Sort-Ordinal
{
    # Sort-Object compares by the current culture, which Windows PowerShell and PowerShell on Linux implement
    # differently (one ignores a hyphen, the other orders it before letters), so the same repository would give the
    # two hosts different reports and different baseline hashes. Ordinal order is the same everywhere.
    param(
        [Parameter(ValueFromPipeline = $true)][AllowNull()][object]$InputObject,
        [string[]]$Property,
        [switch]$Unique
    )

    begin
    {
        $items = [System.Collections.Generic.List[object]]::new()
    }
    process
    {
        if ($null -ne $InputObject)
        {
            $items.Add($InputObject)
        }
    }
    end
    {
        if ($items.Count -eq 0)
        {
            return
        }
        # The indices are sorted by their items' keys, ties by position, so the order is ordinal and stable; an array
        # handed to [Array]::Sort is sorted as a copy.
        $keys = [System.Collections.Generic.List[string]]::new($items.Count)
        $order = [System.Collections.Generic.List[int]]::new($items.Count)
        for ($index = 0; $index -lt $items.Count; ++$index)
        {
            $item = $items[$index]
            if ($Property)
            {
                $keys.Add((@($Property | ForEach-Object { [string]$item.$_ }) -join [string][char]0x1f))
            }
            else
            {
                $keys.Add([string]$item)
            }
            $order.Add($index)
        }
        $order.Sort([System.Comparison[int]] {
            param($left, $right)
            $compared = [string]::CompareOrdinal($keys[$left], $keys[$right])
            if ($compared -ne 0) { return $compared }
            return $left.CompareTo($right)
        })
        $previous = $null
        foreach ($index in $order)
        {
            $key = $keys[$index]
            if ($Unique -and $null -ne $previous -and [string]::Equals($key, $previous, [StringComparison]::Ordinal))
            {
                continue
            }
            $previous = $key
            $items[$index]
        }
    }
}

function Escape-Cell
{
    param([AllowEmptyString()][string]$Value)

    if ([string]::IsNullOrEmpty($Value))
    {
        return "-"
    }
    return (($Value -replace '\|', '\|') -replace "`r?`n", '<br>')
}

function Get-TemplateHeadings
{
    param([Parameter(Mandatory = $true)][string]$TemplateName)

    $template_path = Join-Path $RepositoryRoot "docs/user-guide/templates/$TemplateName"
    $template_text = Read-Text $template_path
    return @([regex]::Matches($template_text, '(?m)^##\s+(.+?)\s*$') |
        ForEach-Object { $_.Groups[1].Value.Trim() })
}

function Get-PageKind
{
    param(
        [Parameter(Mandatory = $true)][string]$PagePath,
        [Parameter(Mandatory = $true)]$Configuration
    )

    $override = $Configuration.pageKindOverrides.PSObject.Properties[$PagePath]
    if ($null -ne $override)
    {
        return [string]$override.Value
    }
    if ($PagePath.StartsWith('processes/'))
    {
        return 'process'
    }
    if ($PagePath.StartsWith('workflows/'))
    {
        return 'workflow'
    }
    if ($PagePath.StartsWith('reference/'))
    {
        return 'reference'
    }
    return 'component'
}

function Get-TrackerRows
{
    param([Parameter(Mandatory = $true)][string]$TrackerText)

    $rows = @{}
    foreach ($line in ($TrackerText -split "`r?`n"))
    {
        if ($line -notmatch '^\| \[[^]]+\]\(([^)]+)\) \|')
        {
            continue
        }
        $fields = @($line.Trim('|').Split('|') | ForEach-Object { $_.Trim() })
        $page_path = $Matches[1] -replace '^docs/en/', ''
        $rows[$page_path] = [pscustomobject]@{
            FieldCount = $fields.Count
            Date = if ($fields.Count -gt 5) { $fields[5] } else { '' }
            Revision = if ($fields.Count -gt 6) { $fields[6] } else { '' }
            SourceRecord = if ($fields.Count -gt 8) { $fields[8] } else { '' }
        }
    }
    return $rows
}

function Get-TrackedSourcePaths
{
    param([Parameter(Mandatory = $true)][string]$SourceRecord)

    $paths = [System.Collections.Generic.List[string]]::new()
    foreach ($match in [regex]::Matches($SourceRecord, '`([^`]+)`'))
    {
        $candidate = $match.Groups[1].Value.Trim() -replace '\\', '/'
        if ($candidate -match '^(src|build|docs|installer|extensions|3rdparty|\.github)/' -or
            $candidate -in @('AGENTS.md', 'README.md'))
        {
            if ($candidate -eq 'docs/user-guide/audits/ug55-reviewed-destinations.json')
            {
                $manifest_path = Join-Path $RepositoryRoot $candidate
                if (Test-Path -LiteralPath $manifest_path -PathType Leaf)
                {
                    $manifest = ([System.IO.File]::ReadAllText($manifest_path)) | ConvertFrom-Json
                    foreach ($manifest_entry in @($manifest.destinations))
                    {
                        $manifest_candidate = $manifest_entry.Trim() -replace '\\', '/'
                        if ($manifest_candidate)
                        {
                            $paths.Add($manifest_candidate)
                        }
                    }
                }
            }
            else
            {
                $paths.Add($candidate)
            }
        }
    }
    return @($paths | Sort-Ordinal -Unique)
}

function Invoke-Git
{
    param([Parameter(Mandatory = $true)][string[]]$Arguments)

    $old_location = Get-Location
    try
    {
        Set-Location $RepositoryRoot
        $result = @(& git @Arguments 2>$null)
        $exit_code = $LASTEXITCODE
    }
    finally
    {
        Set-Location $old_location
    }
    return [pscustomobject]@{ ExitCode = $exit_code; Lines = @($result) }
}

function Invoke-GitOverPaths
{
    # Runs one git command over a pathspec list in parts that each fit one command line, since Windows starts a program
    # with at most 32,767 characters and the pages reviewed at one revision can name more paths than that. A pathspec
    # list matches the union of its paths, so the parts' lines together are the whole list's.
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [AllowEmptyCollection()][string[]]$Paths = @()
    )

    $part_limit = 8000
    $parts = [System.Collections.Generic.List[object]]::new()
    $part = [System.Collections.Generic.List[string]]::new()
    $part_characters = 0
    foreach ($path in $Paths)
    {
        if ($part.Count -gt 0 -and $part_characters + $path.Length + 3 -gt $part_limit)
        {
            $parts.Add($part.ToArray())
            $part = [System.Collections.Generic.List[string]]::new()
            $part_characters = 0
        }
        $part.Add($path)
        $part_characters += $path.Length + 3
    }
    $parts.Add($part.ToArray())

    $lines = [System.Collections.Generic.List[string]]::new()
    foreach ($part_paths in $parts)
    {
        $result = Invoke-Git ($Arguments + @('--') + $part_paths)
        if ($result.ExitCode -ne 0)
        {
            return $result
        }
        foreach ($line in $result.Lines)
        {
            $lines.Add($line)
        }
    }
    return [pscustomobject]@{ ExitCode = 0; Lines = @($lines) }
}

$script:current_repository_snapshot = $null
$script:revision_repository_snapshots = @{}

function New-ResolvedPathSet
{
    param([Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Files)

    $set = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::Ordinal)
    foreach ($file in $Files)
    {
        $candidate = ([string]$file).Trim() -replace '\\', '/'
        while (-not [string]::IsNullOrEmpty($candidate))
        {
            $null = $set.Add($candidate)
            $separator = $candidate.LastIndexOf('/')
            if ($separator -lt 0)
            {
                break
            }
            $candidate = $candidate.Substring(0, $separator)
        }
    }
    return ,$set
}

function Get-CurrentRepositorySnapshot
{
    if ($null -ne $script:current_repository_snapshot)
    {
        return $script:current_repository_snapshot
    }

    $tracked = Invoke-Git @('ls-files')
    $untracked = Invoke-Git @('ls-files', '--others', '--exclude-standard')
    $working = Invoke-Git @('diff', '--name-only', 'HEAD', '--')
    if ($tracked.ExitCode -ne 0 -or $untracked.ExitCode -ne 0 -or $working.ExitCode -ne 0)
    {
        throw 'Unable to capture the current repository state for source-drift auditing.'
    }

    $known_files = @($tracked.Lines + $untracked.Lines | Sort-Ordinal -Unique)
    $changed_files = @($working.Lines + $untracked.Lines | Sort-Ordinal -Unique)
    $script:current_repository_snapshot = [pscustomobject]@{
        ResolvedPaths = New-ResolvedPathSet $known_files
        ChangedFiles = $changed_files
    }
    return $script:current_repository_snapshot
}

function Get-RevisionRepositorySnapshot
{
    param(
        [Parameter(Mandatory = $true)][string]$Revision,
        [AllowEmptyCollection()][string[]]$SourcePaths = @()
    )

    if ($script:revision_repository_snapshots.ContainsKey($Revision))
    {
        return $script:revision_repository_snapshots[$Revision]
    }

    $commit_check = Invoke-Git @('cat-file', '-e', "${Revision}^{commit}")
    if ($commit_check.ExitCode -ne 0)
    {
        $snapshot = [pscustomobject]@{ Available = $false }
        $script:revision_repository_snapshots[$Revision] = $snapshot
        return $snapshot
    }

    $tree = Invoke-GitOverPaths @('ls-tree', '-r', '--name-only', $Revision) $SourcePaths
    $committed_changes = Invoke-GitOverPaths @('diff', '--name-only', $Revision, 'HEAD') $SourcePaths
    if ($tree.ExitCode -ne 0 -or $committed_changes.ExitCode -ne 0)
    {
        throw "Unable to capture repository state for reviewed revision $Revision."
    }

    $current = Get-CurrentRepositorySnapshot
    $snapshot = [pscustomobject]@{
        Available = $true
        ResolvedPaths = New-ResolvedPathSet @($tree.Lines)
        ChangedFiles = @($committed_changes.Lines + $current.ChangedFiles | Sort-Ordinal -Unique)
    }
    $script:revision_repository_snapshots[$Revision] = $snapshot
    return $snapshot
}

function Test-SourcePathMatchesFile
{
    param(
        [Parameter(Mandatory = $true)][string]$SourcePath,
        [Parameter(Mandatory = $true)][string]$FilePath
    )

    return $FilePath -eq $SourcePath -or
        $FilePath.StartsWith("$SourcePath/", [System.StringComparison]::Ordinal)
}

function Get-SourceDrift
{
    param(
        [Parameter(Mandatory = $true)][string]$Revision,
        [Parameter(Mandatory = $true)][AllowNull()][AllowEmptyCollection()][string[]]$SourcePaths
    )

    if ($Revision -notmatch '(?i)\b[0-9a-f]{40}\b')
    {
        return [pscustomobject]@{ Status = 'Unavailable'; Detail = 'reviewed revision missing or invalid' }
    }
    $revision_sha = $Matches[0]
    $revision_snapshot = Get-RevisionRepositorySnapshot $revision_sha
    if (-not $revision_snapshot.Available)
    {
        return [pscustomobject]@{ Status = 'Unavailable'; Detail = "reviewed revision is not present: $revision_sha" }
    }
    if ($SourcePaths.Count -eq 0)
    {
        return [pscustomobject]@{ Status = 'Unavailable'; Detail = 'no repository-relative source paths recorded' }
    }

    $current_snapshot = Get-CurrentRepositorySnapshot
    $resolved_paths = [System.Collections.Generic.List[string]]::new()
    $unresolved_paths = [System.Collections.Generic.List[string]]::new()
    foreach ($source_path in $SourcePaths)
    {
        $normalized = $source_path.Trim().TrimEnd('/') -replace '\\', '/'
        if ($current_snapshot.ResolvedPaths.Contains($normalized) -or
            $revision_snapshot.ResolvedPaths.Contains($normalized))
        {
            $resolved_paths.Add($normalized)
        }
        else
        {
            $unresolved_paths.Add($normalized)
        }
    }
    $resolved = @($resolved_paths | Sort-Ordinal -Unique)
    if ($resolved.Count -eq 0)
    {
        return [pscustomobject]@{
            Status = 'Unavailable'
            Detail = ('recorded paths do not resolve at the reviewed revision or in the current tree: ' +
                ($unresolved_paths -join ', '))
        }
    }

    $changed_path_set = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::Ordinal)
    foreach ($changed_file in $revision_snapshot.ChangedFiles)
    {
        foreach ($source_path in $resolved)
        {
            if (Test-SourcePathMatchesFile $source_path $changed_file)
            {
                $null = $changed_path_set.Add($changed_file)
                break
            }
        }
    }
    $changed_paths = @($changed_path_set | Sort-Ordinal)
    $details = [System.Collections.Generic.List[string]]::new()
    if ($changed_paths.Count -gt 0)
    {
        $details.Add("changed since revision: $($changed_paths -join ', ')")
    }
    if ($unresolved_paths.Count -gt 0)
    {
        $details.Add("unresolved records: $($unresolved_paths -join ', ')")
    }
    if ($changed_paths.Count -gt 0)
    {
        return [pscustomobject]@{ Status = 'Drift'; Detail = $details -join '; ' }
    }
    if ($unresolved_paths.Count -gt 0)
    {
        return [pscustomobject]@{ Status = 'Partial'; Detail = $details -join '; ' }
    }
    return [pscustomobject]@{ Status = 'Current'; Detail = 'no recorded source path changed' }
}

function Find-ParameterTable
{
    param([Parameter(Mandatory = $true)][string]$PageText)

    $heading_match = [regex]::Match(
        $PageText,
        '(?im)^##\s+(Complete parameter and action reference|Parameters?|Controls?(?: and actions?)?)\s*$')
    if (-not $heading_match.Success)
    {
        return [pscustomobject]@{ Status = 'Missing'; Detail = 'no parameter/control section'; Rows = 0 }
    }

    $tail = $PageText.Substring($heading_match.Index + $heading_match.Length)
    $next_heading = [regex]::Match($tail, '(?m)^##\s+')
    if ($next_heading.Success)
    {
        $tail = $tail.Substring(0, $next_heading.Index)
    }
    $table_blocks = @()
    $current_block = [System.Collections.Generic.List[string]]::new()
    foreach ($line in ($tail -split "`r?`n"))
    {
        if ($line -match '^\|.*\|\s*$')
        {
            $current_block.Add($line)
            continue
        }
        if ($current_block.Count -gt 0)
        {
            $table_blocks += , @($current_block)
            $current_block = [System.Collections.Generic.List[string]]::new()
        }
    }
    if ($current_block.Count -gt 0)
    {
        $table_blocks += , @($current_block)
    }

    if ($table_blocks.Count -eq 0)
    {
        return [pscustomobject]@{ Status = 'Malformed'; Detail = 'section has no Markdown table'; Rows = 0 }
    }

    $required_terms = @(
        'ui label', 'type and unit', 'runtime default', 'persisted-default', 'valid values',
        'effect of', 'dependencies', 'trade-off', 'scope:', 'persistence'
    )
    $missing_terms = [System.Collections.Generic.HashSet[string]]::new()
    $data_rows = 0
    $parameter_table_count = 0
    $table_block_index = 0
    foreach ($table_block in $table_blocks)
    {
        $header = @($table_block[0].Trim('|').Split('|') | ForEach-Object { $_.Trim().ToLowerInvariant() })
        $header_missing_terms = @($required_terms | Where-Object {
            $term = $_
            -not ($header | Where-Object { $_.Contains($term) })
        })
        $is_parameter_table = $table_block_index -eq 0 -or $header_missing_terms.Count -eq 0
        ++$table_block_index
        if (-not $is_parameter_table)
        {
            continue
        }

        ++$parameter_table_count
        if ($table_block.Count -lt 2 -or $table_block[1] -notmatch '^\|\s*:?-{3,}')
        {
            return [pscustomobject]@{
                Status = 'Malformed'
                Detail = 'section contains a malformed parameter table'
                Rows = 0
            }
        }

        foreach ($term in $header_missing_terms)
        {
            [void]$missing_terms.Add($term)
        }
        $data_rows += (0, ($table_block.Count - 2) | Measure-Object -Maximum).Maximum
    }
    if ($parameter_table_count -eq 0)
    {
        return [pscustomobject]@{ Status = 'Malformed'; Detail = 'section has no parameter table'; Rows = 0 }
    }
    $missing_terms = @($missing_terms | Sort-Ordinal)
    if ($missing_terms.Count -gt 0)
    {
        return [pscustomobject]@{
            Status = 'Noncanonical shape'
            Detail = "missing columns: $($missing_terms -join ', ')"
            Rows = $data_rows
        }
    }
    if ($data_rows -eq 0)
    {
        return [pscustomobject]@{ Status = 'Empty'; Detail = 'canonical columns but no data rows'; Rows = 0 }
    }
    return [pscustomobject]@{ Status = 'Canonical'; Detail = 'all required columns present'; Rows = $data_rows }
}

$config = (Read-Text $config_path) | ConvertFrom-Json
$mkdocs_text = Read-Text $mkdocs_path
$tracker_text = Read-Text $tracker_path
$registry_text = Read-Text $registry_path
$ui_text = Read-Text $ui_builder_path

$nav_entries = [System.Collections.Generic.List[object]]::new()
foreach ($line in ($mkdocs_text -split "`r?`n"))
{
    if ($line -match '^\s+-\s+(.+?):\s+([^#]+\.md)\s*$')
    {
        $nav_entries.Add([pscustomobject]@{ Title = $Matches[1].Trim(); Path = $Matches[2].Trim() })
    }
}
$nav_entries = @($nav_entries | Sort-Ordinal -Property Path)
$nav_by_path = @{}
$nav_by_title = @{}
foreach ($entry in $nav_entries)
{
    $nav_by_path[$entry.Path] = $entry
    $normalized_title = Normalize-Title $entry.Title
    if (-not $nav_by_title.ContainsKey($normalized_title))
    {
        $nav_by_title[$normalized_title] = [System.Collections.Generic.List[object]]::new()
    }
    $nav_by_title[$normalized_title].Add($entry)
}

$tracker_rows = Get-TrackerRows $tracker_text
$source_paths_by_revision = @{}
foreach ($tracker_row in $tracker_rows.Values)
{
    if ($tracker_row.Revision -notmatch '(?i)\b[0-9a-f]{40}\b')
    {
        continue
    }
    $revision = $Matches[0]
    if (-not $source_paths_by_revision.ContainsKey($revision))
    {
        $source_paths_by_revision[$revision] = [System.Collections.Generic.HashSet[string]]::new(
            [System.StringComparer]::Ordinal)
    }
    foreach ($source_path in @(Get-TrackedSourcePaths $tracker_row.SourceRecord))
    {
        $null = $source_paths_by_revision[$revision].Add($source_path)
    }
}
foreach ($revision in @($source_paths_by_revision.Keys | Sort-Ordinal))
{
    $revision_source_paths = @($source_paths_by_revision[$revision] | Sort-Ordinal)
    $null = Get-RevisionRepositorySnapshot $revision $revision_source_paths
}
$template_headings = @{
    component = Get-TemplateHeadings 'COMPONENT_PAGE_TEMPLATE.md'
    process = Get-TemplateHeadings 'PROCESS_PAGE_TEMPLATE.md'
    reference = Get-TemplateHeadings 'REFERENCE_PAGE_TEMPLATE.md'
    workflow = Get-TemplateHeadings 'WORKFLOW_PAGE_TEMPLATE.md'
}

$action_labels = @{}
foreach ($match in [regex]::Matches(
    $ui_text,
    '(?s)(action[A-Za-z0-9_]+)->setText\(.*?QCoreApplication::translate\("AfterNightAppClass",\s*"([^"]+)".*?\)\s*\);'))
{
    $action_labels[$match.Groups[1].Value] = $match.Groups[2].Value.Replace('&', '')
}

$menu_labels = @{}
foreach ($match in [regex]::Matches(
    $ui_text,
    '(?s)(menu[A-Za-z0-9_]+)->setTitle\(.*?QCoreApplication::translate\("AfterNightAppClass",\s*"([^"]+)".*?\)\s*\);'))
{
    $menu_labels[$match.Groups[1].Value] = $match.Groups[2].Value.Replace('&', '')
}

$menus_by_action = @{}
foreach ($match in [regex]::Matches($ui_text, '(?m)^\s*(menu[A-Za-z0-9_]+)->addAction\((action[A-Za-z0-9_]+)\);'))
{
    $menu_id = $match.Groups[1].Value
    $action_id = $match.Groups[2].Value
    if (-not $menus_by_action.ContainsKey($action_id))
    {
        $menus_by_action[$action_id] = [System.Collections.Generic.List[string]]::new()
    }
    $menus_by_action[$action_id].Add($menu_id)
}

$child_menus = @{}
foreach ($match in [regex]::Matches(
    $ui_text,
    '(?m)^\s*(menu[A-Za-z0-9_]+)->addAction\((menu[A-Za-z0-9_]+)->menuAction\(\)\);'))
{
    $parent = $match.Groups[1].Value
    if (-not $child_menus.ContainsKey($parent))
    {
        $child_menus[$parent] = [System.Collections.Generic.List[string]]::new()
    }
    $child_menus[$parent].Add($match.Groups[2].Value)
}

function Get-DescendantMenuActions
{
    param([Parameter(Mandatory = $true)][string]$MenuId)

    $found = [System.Collections.Generic.List[string]]::new()
    foreach ($property in $menus_by_action.GetEnumerator())
    {
        if ($property.Value -contains $MenuId)
        {
            $found.Add($property.Key)
        }
    }
    if ($child_menus.ContainsKey($MenuId))
    {
        foreach ($child in $child_menus[$MenuId])
        {
            foreach ($action in (Get-DescendantMenuActions $child))
            {
                $found.Add($action)
            }
        }
    }
    return @($found | Sort-Ordinal -Unique)
}

$category_constants = @{}
foreach ($match in [regex]::Matches(
    $registry_text,
    'constexpr\s+const\s+char\*\s+(kCategory[A-Za-z0-9_]+)\s*=\s*"([^"]+)"'))
{
    $category_constants[$match.Groups[1].Value] = $match.Groups[2].Value
}

$registered_actions = [System.Collections.Generic.List[object]]::new()
$registration_pattern = '(?ms)^\s+AN_REGISTER_(PROCESS_(?:SINGLE|TOPLEVEL|NON_INTERACTIVE)|CHANNEL|IMAGE_OPERATION)\((.*?)\);'
foreach ($match in [regex]::Matches($registry_text, $registration_pattern))
{
    $macro_kind = $match.Groups[1].Value
    $arguments = $match.Groups[2].Value
    $action_match = [regex]::Match($arguments, '\b(action[A-Za-z0-9_]+)\b')
    if (-not $action_match.Success)
    {
        continue
    }
    $category = 'non_interactive'
    if ($macro_kind.StartsWith('PROCESS_'))
    {
        $category_token = ($arguments -split ',')[0].Trim()
        if ($category_constants.ContainsKey($category_token))
        {
            $category = $category_constants[$category_token]
        }
        else
        {
            $category = $category_token
        }
    }
    $registered_actions.Add([pscustomobject]@{
        Id = $action_match.Value
        MacroKind = $macro_kind
        Category = $category
    })
}
$registered_actions = @($registered_actions | Sort-Ordinal -Property Id)
$registered_ids = @($registered_actions | ForEach-Object { $_.Id })
$process_menu_actions = @(Get-DescendantMenuActions 'menuProcess')
$image_menu_actions = @(Get-DescendantMenuActions 'menuImage')
$process_menu_only_actions = @($process_menu_actions | Where-Object { $_ -notin $registered_ids } | Sort-Ordinal)
$image_menu_only_actions = @($image_menu_actions | Where-Object { $_ -notin $registered_ids } | Sort-Ordinal)

function Resolve-GuideDestination
{
    param(
        [Parameter(Mandatory = $true)][string]$ActionId,
        [AllowEmptyString()][string]$MacroKind,
        [ValidateSet('', 'Process', 'Image')][string]$DirectMenu = ''
    )

    if (-not [string]::IsNullOrEmpty($DirectMenu))
    {
        $override_name = if ($DirectMenu -eq 'Process') {
            'processMenuOnlyDestinationOverrides'
        } else {
            'imageMenuOnlyDestinationOverrides'
        }
        $menu_override = $config.$override_name.PSObject.Properties[$ActionId]
        if ($null -ne $menu_override)
        {
            return [string]$menu_override.Value
        }
        return $null
    }
    $override = $config.destinationOverrides.PSObject.Properties[$ActionId]
    if ($null -ne $override)
    {
        return [string]$override.Value
    }
    if ($MacroKind -in @($config.consolidatedMacroKinds) -or $ActionId -in @($config.consolidatedActions))
    {
        return [string]$config.consolidatedDestination
    }
    if (-not $action_labels.ContainsKey($ActionId))
    {
        return $null
    }
    $title_key = Normalize-Title $action_labels[$ActionId]
    $matches = @($nav_by_title[$title_key] | Where-Object { $_.Path.StartsWith('processes/') })
    if ($matches.Count -eq 1)
    {
        return $matches[0].Path
    }
    return $null
}

$parity_findings = [System.Collections.Generic.List[object]]::new()
$coverage_rows = [System.Collections.Generic.List[object]]::new()
$direct_menu_coverage_rows = [System.Collections.Generic.List[object]]::new()
foreach ($registered_action in $registered_actions)
{
    $action_id = $registered_action.Id
    $label = if ($action_labels.ContainsKey($action_id)) { $action_labels[$action_id] } else { '' }
    $menus = if ($menus_by_action.ContainsKey($action_id)) { @($menus_by_action[$action_id] | Sort-Ordinal -Unique) } else { @() }
    $menu_names = @($menus | ForEach-Object {
        if ($menu_labels.ContainsKey($_)) { $menu_labels[$_] } else { $_ }
    })
    $destination = Resolve-GuideDestination $action_id $registered_action.MacroKind
    $status = 'Covered'
    if ([string]::IsNullOrEmpty($label))
    {
        $parity_findings.Add([pscustomobject]@{
            Code = 'REGISTRY_LABEL_MISSING'; Subject = $action_id; Detail = 'no generated QAction label found'
        })
        $status = 'Finding'
    }
    if ($menus.Count -eq 0)
    {
        $parity_findings.Add([pscustomobject]@{
            Code = 'REGISTRY_MENU_MISSING'; Subject = $action_id; Detail = 'registered action is not added to a generated menu'
        })
        $status = 'Finding'
    }
    if ([string]::IsNullOrEmpty($destination))
    {
        $parity_findings.Add([pscustomobject]@{
            Code = 'GUIDE_DESTINATION_UNRESOLVED'; Subject = $action_id; Detail = "label '$label' has no unique process navigation match or override"
        })
        $status = 'Finding'
    }
    else
    {
        if (-not $nav_by_path.ContainsKey($destination))
        {
            $parity_findings.Add([pscustomobject]@{
                Code = 'GUIDE_DESTINATION_NOT_NAVIGATED'; Subject = $action_id; Detail = $destination
            })
            $status = 'Finding'
        }
        if (-not (Test-Path -LiteralPath (Join-Path $docs_root $destination) -PathType Leaf))
        {
            $parity_findings.Add([pscustomobject]@{
                Code = 'GUIDE_DESTINATION_MISSING'; Subject = $action_id; Detail = $destination
            })
            $status = 'Finding'
        }
    }
    $coverage_rows.Add([pscustomobject]@{
        Id = $action_id
        Label = $label
        Menus = $menu_names -join ', '
        Destination = $destination
        Status = $status
    })
}

foreach ($action_id in $process_menu_only_actions)
{
    $destination = Resolve-GuideDestination $action_id '' -DirectMenu Process
    $status = 'Covered'
    if ([string]::IsNullOrEmpty($destination))
    {
        $parity_findings.Add([pscustomobject]@{
            Code = 'PROCESS_MENU_DESTINATION_UNRESOLVED'
            Subject = $action_id
            Detail = 'direct-wired Process menu action has no guide override'
        })
        $status = 'Finding'
    }
    elseif (-not $nav_by_path.ContainsKey($destination) -or
            -not (Test-Path -LiteralPath (Join-Path $docs_root $destination) -PathType Leaf))
    {
        $parity_findings.Add([pscustomobject]@{
            Code = 'PROCESS_MENU_DESTINATION_MISSING'
            Subject = $action_id
            Detail = $destination
        })
        $status = 'Finding'
    }
    $direct_menu_coverage_rows.Add([pscustomobject]@{
        Menu = 'Process'; Id = $action_id; Label = $action_labels[$action_id]
        Destination = $destination; Status = $status
    })
}

foreach ($action_id in $image_menu_only_actions)
{
    $destination = Resolve-GuideDestination $action_id '' -DirectMenu Image
    $status = 'Covered'
    if ([string]::IsNullOrEmpty($destination))
    {
        $parity_findings.Add([pscustomobject]@{
            Code = 'IMAGE_MENU_DESTINATION_UNRESOLVED'
            Subject = $action_id
            Detail = 'direct-wired Image menu action has no guide override'
        })
        $status = 'Finding'
    }
    elseif (-not $nav_by_path.ContainsKey($destination) -or
            -not (Test-Path -LiteralPath (Join-Path $docs_root $destination) -PathType Leaf))
    {
        $parity_findings.Add([pscustomobject]@{
            Code = 'IMAGE_MENU_DESTINATION_MISSING'
            Subject = $action_id
            Detail = $destination
        })
        $status = 'Finding'
    }
    $direct_menu_coverage_rows.Add([pscustomobject]@{
        Menu = 'Image'; Id = $action_id; Label = $action_labels[$action_id]
        Destination = $destination; Status = $status
    })
}

foreach ($entry in $nav_entries)
{
    if (-not (Test-Path -LiteralPath (Join-Path $docs_root $entry.Path) -PathType Leaf))
    {
        $parity_findings.Add([pscustomobject]@{
            Code = 'NAV_DESTINATION_MISSING'; Subject = $entry.Title; Detail = $entry.Path
        })
    }
    if (-not $tracker_rows.ContainsKey($entry.Path))
    {
        $parity_findings.Add([pscustomobject]@{
            Code = 'TRACKER_ROW_MISSING'; Subject = $entry.Title; Detail = $entry.Path
        })
    }
}
foreach ($tracker_page in @($tracker_rows.Keys | Sort-Ordinal))
{
    if (-not $nav_by_path.ContainsKey($tracker_page))
    {
        $parity_findings.Add([pscustomobject]@{
            Code = 'TRACKER_PAGE_NOT_NAVIGATED'; Subject = $tracker_page; Detail = 'tracker row has no nav target'
        })
    }
}
$parity_findings = @($parity_findings | Sort-Ordinal -Property Code, Subject, Detail)

$page_rows = [System.Collections.Generic.List[object]]::new()
$heading_findings = 0
$pages_missing_headings = 0
$missing_review_dates = 0
$missing_review_revisions = 0
$drift_pages = 0
$partial_source_pages = 0
$unavailable_source_pages = 0
$boilerplate_pages = 0
$parameter_status_counts = @{}
foreach ($entry in $nav_entries)
{
    $page_path = Join-Path $docs_root $entry.Path
    $page_exists = Test-Path -LiteralPath $page_path -PathType Leaf
    $page_text = if ($page_exists) { Read-Text $page_path } else { '' }
    $kind = Get-PageKind $entry.Path $config
    $actual_headings = @([regex]::Matches($page_text, '(?m)^##\s+(.+?)\s*$') |
        ForEach-Object { $_.Groups[1].Value.Trim() })
    $required_headings = @($template_headings[$kind])
    $missing_headings = @($required_headings | Where-Object { $_ -notin $actual_headings })
    if ($missing_headings.Count -gt 0)
    {
        ++$pages_missing_headings
        $heading_findings += $missing_headings.Count
    }

    $tracker_row = if ($tracker_rows.ContainsKey($entry.Path)) { $tracker_rows[$entry.Path] } else { $null }
    $review_date = if ($null -ne $tracker_row) { $tracker_row.Date } else { '' }
    $review_revision = if ($null -ne $tracker_row) { $tracker_row.Revision } else { '' }
    $date_current = $review_date -match '^\d{4}-\d{2}-\d{2}$'
    $revision_current = $review_revision -match '(?i)\b[0-9a-f]{40}\b'
    if (-not $date_current) { ++$missing_review_dates }
    if (-not $revision_current) { ++$missing_review_revisions }
    $source_paths = if ($null -ne $tracker_row) { @(Get-TrackedSourcePaths $tracker_row.SourceRecord) } else { @() }
    $drift = Get-SourceDrift $review_revision $source_paths
    if ($drift.Status -eq 'Drift') { ++$drift_pages }
    if ($drift.Status -eq 'Partial') { ++$partial_source_pages }
    if ($drift.Status -eq 'Unavailable') { ++$unavailable_source_pages }

    $boilerplate_ids = [System.Collections.Generic.List[string]]::new()
    foreach ($signature in $config.genericBoilerplate)
    {
        if ($page_text.Contains([string]$signature.text))
        {
            $boilerplate_ids.Add([string]$signature.id)
        }
    }
    if ($boilerplate_ids.Count -gt 0) { ++$boilerplate_pages }

    $parameter_table = if ($kind -eq 'process') {
        Find-ParameterTable $page_text
    } else {
        [pscustomobject]@{ Status = 'N/A'; Detail = 'not a process page'; Rows = 0 }
    }
    if (-not $parameter_status_counts.ContainsKey($parameter_table.Status))
    {
        $parameter_status_counts[$parameter_table.Status] = 0
    }
    ++$parameter_status_counts[$parameter_table.Status]

    $page_rows.Add([pscustomobject]@{
        Path = $entry.Path
        Kind = $kind
        MissingHeadings = $missing_headings
        ReviewDate = if ($date_current) { $review_date } else { 'Not recorded' }
        ReviewRevision = if ($revision_current) { $Matches[0] } else { 'Not recorded' }
        Drift = $drift
        Boilerplate = @($boilerplate_ids)
        Parameter = $parameter_table
    })
}

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('# User Guide Audit Report')
$lines.Add('')
$lines.Add('This report is generated by `build/scripts/audit_user_guide.ps1`. It is intentionally')
$lines.Add('date-free so identical repository content produces identical output. Findings are the')
$lines.Add('explicit UG-01 starting baseline; later phases must regenerate it when they change')
$lines.Add('audited navigation, tracker metadata, page structure, or process coverage.')
$lines.Add('')
$lines.Add('## Summary')
$lines.Add('')
$lines.Add('| Measure | Result |')
$lines.Add('| --- | ---: |')
$lines.Add("| Navigated pages | $($nav_entries.Count) |")
$lines.Add("| Tracker page rows | $($tracker_rows.Count) |")
$lines.Add("| Registered built-in actions | $($registered_actions.Count) |")
$lines.Add("| Registered actions with no menu | $(@($registered_actions | Where-Object { -not $menus_by_action.ContainsKey($_.Id) }).Count) |")
$lines.Add("| Direct-wired Process menu actions outside the registry | $($process_menu_only_actions.Count) |")
$lines.Add("| Direct-wired Image menu actions outside the registry | $($image_menu_only_actions.Count) |")
$lines.Add("| Parity findings | $($parity_findings.Count) |")
$lines.Add("| Pages missing one or more required headings | $pages_missing_headings |")
$lines.Add("| Required-heading findings | $heading_findings |")
$lines.Add("| Pages without a current source-review date | $missing_review_dates |")
$lines.Add("| Pages without a full reviewed revision | $missing_review_revisions |")
$lines.Add("| Pages with recorded source drift | $drift_pages |")
$lines.Add("| Pages with partial source records | $partial_source_pages |")
$lines.Add("| Pages with unavailable source review | $unavailable_source_pages |")
$lines.Add("| Pages matching generic-boilerplate signatures | $boilerplate_pages |")
$lines.Add('')
$lines.Add('## Registry, menu, and guide parity findings')
$lines.Add('')
if ($parity_findings.Count -eq 0)
{
    $lines.Add('No parity findings.')
}
else
{
    $lines.Add('| Code | Subject | Detail |')
    $lines.Add('| --- | --- | --- |')
    foreach ($finding in $parity_findings)
    {
        $lines.Add("| $($finding.Code) | $(Escape-Cell $finding.Subject) | $(Escape-Cell $finding.Detail) |")
    }
}
$lines.Add('')
$lines.Add('## Registered action coverage')
$lines.Add('')
$lines.Add('| Action | UI label | Menu(s) | Guide destination | Status |')
$lines.Add('| --- | --- | --- | --- | --- |')
foreach ($row in $coverage_rows)
{
    $lines.Add("| ``$($row.Id)`` | $(Escape-Cell $row.Label) | $(Escape-Cell $row.Menus) | $(Escape-Cell $row.Destination) | $($row.Status) |")
}
$lines.Add('')
$lines.Add('## Direct-wired menu action coverage')
$lines.Add('')
$lines.Add('| Menu | Action | UI label | Guide destination | Status |')
$lines.Add('| --- | --- | --- | --- | --- |')
foreach ($row in $direct_menu_coverage_rows)
{
    $lines.Add("| $($row.Menu) | ``$($row.Id)`` | $(Escape-Cell $row.Label) | $(Escape-Cell $row.Destination) | $($row.Status) |")
}
$lines.Add('')
$lines.Add('## Generic-boilerplate signatures')
$lines.Add('')
$lines.Add('| ID | Exact signature | Matching pages |')
$lines.Add('| --- | --- | ---: |')
foreach ($signature in $config.genericBoilerplate)
{
    $match_count = @($page_rows | Where-Object { $_.Boilerplate -contains $signature.id }).Count
    $lines.Add("| $($signature.id) | $(Escape-Cell $signature.text) | $match_count |")
}
$lines.Add('')
$lines.Add('## Page-depth and source-review report')
$lines.Add('')
$lines.Add('| Page | Kind | Missing required headings | Review date | Reviewed revision | Source drift | Boilerplate | Parameter table |')
$lines.Add('| --- | --- | --- | --- | --- | --- | --- | --- |')
foreach ($row in $page_rows)
{
    $missing = if ($row.MissingHeadings.Count -eq 0) { 'None' } else { $row.MissingHeadings -join '; ' }
    $drift_text = "$($row.Drift.Status): $($row.Drift.Detail)"
    $boilerplate = if ($row.Boilerplate.Count -eq 0) { 'None' } else { $row.Boilerplate -join ', ' }
    $parameter = if ($row.Parameter.Status -eq 'N/A') {
        'N/A'
    } else {
        "$($row.Parameter.Status) ($($row.Parameter.Rows) row(s)): $($row.Parameter.Detail)"
    }
    $lines.Add("| $($row.Path) | $($row.Kind) | $(Escape-Cell $missing) | $($row.ReviewDate) | $($row.ReviewRevision) | $(Escape-Cell $drift_text) | $boilerplate | $(Escape-Cell $parameter) |")
}
$lines.Add('')
$lines.Add('## Parameter-table summary')
$lines.Add('')
$lines.Add('| Status | Page count |')
$lines.Add('| --- | ---: |')
foreach ($status in @($parameter_status_counts.Keys | Sort-Ordinal))
{
    $lines.Add("| $status | $($parameter_status_counts[$status]) |")
}
$lines.Add('')
$lines.Add('See `docs/user-guide/audits/README.md` for scope, interpretation, and limitations.')
$report = ($lines -join "`n") + "`n"

function Get-StructuralBaselineReport
{
    param([Parameter(Mandatory = $true)][string]$Report)

    $normalized = [regex]::Replace(
        $Report,
        '(?m)^\| Pages with recorded source drift \| \d+ \|$',
        '| Pages with recorded source drift | <volatile> |')

    $section_heading = '## Page-depth and source-review report'
    $section_start = $normalized.IndexOf($section_heading, [System.StringComparison]::Ordinal)
    if ($section_start -lt 0)
    {
        throw 'The audit report is missing its page-depth section.'
    }
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
        if ($cells.Count -ne 8)
        {
            throw "Unexpected page-depth report row: $line"
        }
        $cells[5] = '<volatile source drift>'
        $section_lines[$index] = $cells -join ' | '
    }
    $normalized_section = $section_lines -join "`n"
    return $normalized.Substring(0, $section_start) + $normalized_section +
        $normalized.Substring($section_end)
}

if ($CheckBaseline)
{
    $baseline = (Read-Text $baseline_path) | ConvertFrom-Json
    $baseline_report = Get-StructuralBaselineReport $report
    $report_bytes = [System.Text.UTF8Encoding]::new($false).GetBytes($baseline_report)
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try
    {
        $report_hash = ([System.BitConverter]::ToString($sha256.ComputeHash($report_bytes))).Replace('-', '').ToLowerInvariant()
    }
    finally
    {
        $sha256.Dispose()
    }
    if ($report_hash -ne [string]$baseline.reportSha256)
    {
        throw "User-guide audit structure differs from $baseline_path (expected $($baseline.reportSha256), actual $report_hash). Regenerate and review the report before updating the baseline hash. Source-drift details are intentionally excluded from this comparison."
    }
}

if (-not [string]::IsNullOrWhiteSpace($OutputPath))
{
    if (-not [System.IO.Path]::IsPathRooted($OutputPath))
    {
        $OutputPath = Join-Path $RepositoryRoot $OutputPath
    }
    Write-Utf8NoBom $OutputPath $report
}
else
{
    [Console]::Out.Write($report)
}

if ($FailOnFindings -and
    ($parity_findings.Count -gt 0 -or $heading_findings -gt 0 -or
     $missing_review_dates -gt 0 -or $missing_review_revisions -gt 0 -or
     $drift_pages -gt 0 -or $partial_source_pages -gt 0 -or
     $unavailable_source_pages -gt 0 -or $boilerplate_pages -gt 0 -or
     @($page_rows | Where-Object { $_.Kind -eq 'process' -and $_.Parameter.Status -ne 'Canonical' }).Count -gt 0))
{
    throw 'User-guide audit contains findings.'
}
