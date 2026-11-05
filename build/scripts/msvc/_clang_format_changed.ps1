param(
    [ValidateSet("Check", "Format")]
    [string]$Mode = "Check",

    [string]$BaseRef = ""
)

$ErrorActionPreference = "Stop"

function Resolve-CommandPath {
    param([string[]]$Candidates)

    foreach ($candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }

        $command = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($command) {
            return $command.Source
        }

        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    return $null
}

function Exit-WithMessage {
    param(
        [string]$Message,
        [int]$Code
    )

    Write-Host $Message
    exit $Code
}

function Test-ByteArrayEqual {
    param(
        [byte[]]$Left,
        [byte[]]$Right
    )

    if ($Left.Length -ne $Right.Length) {
        return $false
    }

    for ($i = 0; $i -lt $Left.Length; ++$i) {
        if ($Left[$i] -ne $Right[$i]) {
            return $false
        }
    }

    return $true
}

function Get-LocalLineEnding {
    param(
        [byte[]]$Bytes,
        [int]$Offset,
        [int]$Length
    )

    $end = [Math]::Min($Bytes.Length, $Offset + $Length)
    for ($i = $Offset; $i -lt $end; ++$i) {
        if ($Bytes[$i] -eq 10) {
            if ($i -gt $Offset -and $Bytes[$i - 1] -eq 13) {
                return "`r`n"
            }
            return "`n"
        }
    }

    for ($i = [Math]::Min($Offset, $Bytes.Length - 1); $i -ge 0; --$i) {
        if ($Bytes[$i] -eq 10) {
            if ($i -gt 0 -and $Bytes[$i - 1] -eq 13) {
                return "`r`n"
            }
            return "`n"
        }
    }

    for ($i = $end; $i -lt $Bytes.Length; ++$i) {
        if ($Bytes[$i] -eq 10) {
            if ($i -gt 0 -and $Bytes[$i - 1] -eq 13) {
                return "`r`n"
            }
            return "`n"
        }
    }

    return "`n"
}

function Convert-ReplacementTextToBytes {
    param(
        [string]$Text,
        [string]$LineEnding
    )

    $normalized = $Text -replace "`r`n", "`n"
    $normalized = $normalized -replace "`r", "`n"
    if ($LineEnding -eq "`r`n") {
        $normalized = $normalized -replace "`n", "`r`n"
    }

    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    return ,$utf8NoBom.GetBytes($normalized)
}

function Convert-BytesToNormalizedText {
    param([byte[]]$Bytes)

    if ($null -eq $Bytes) {
        return ""
    }

    $text = [System.Text.Encoding]::UTF8.GetString($Bytes)
    $text = $text -replace "`r`n", "`n"
    $text = $text -replace "`r", "`n"
    return $text
}

function Get-BytesSlice {
    param(
        [byte[]]$Bytes,
        [int]$Offset,
        [int]$Length
    )

    if ($Length -le 0) {
        return ,[byte[]]::new(0)
    }

    $slice = [byte[]]::new($Length)
    [System.Buffer]::BlockCopy($Bytes, $Offset, $slice, 0, $Length)
    return ,$slice
}

function Set-BytesRange {
    param(
        [byte[]]$Bytes,
        [int]$Offset,
        [int]$Length,
        [byte[]]$Replacement
    )

    $newLength = $Bytes.Length - $Length + $Replacement.Length
    $result = [byte[]]::new($newLength)

    if ($Offset -gt 0) {
        [System.Buffer]::BlockCopy($Bytes, 0, $result, 0, $Offset)
    }

    if ($Replacement.Length -gt 0) {
        [System.Buffer]::BlockCopy($Replacement, 0, $result, $Offset, $Replacement.Length)
    }

    $suffixOffset = $Offset + $Length
    $suffixLength = $Bytes.Length - $suffixOffset
    if ($suffixLength -gt 0) {
        [System.Buffer]::BlockCopy($Bytes, $suffixOffset, $result, $Offset + $Replacement.Length, $suffixLength)
    }

    return ,$result
}

function Get-FormattedBytes {
    param(
        [string]$Path,
        [string[]]$LineArgs
    )

    $originalBytes = [System.IO.File]::ReadAllBytes($Path)
    $xmlText = & $clangFormat --output-replacements-xml @LineArgs $Path
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    [xml]$xml = ($xmlText -join "`n")
    if ($xml.replacements.incomplete_format -eq "true") {
        Exit-WithMessage "clang-format reported incomplete formatting for $Path" 1
    }

    $nodes = @($xml.replacements.replacement)
    if ($nodes.Count -eq 0) {
        return ,$originalBytes
    }

    $formattedBytes = $originalBytes
    $replacements = $nodes | Sort-Object { [int]$_.offset } -Descending
    foreach ($replacement in $replacements) {
        $offset = [int]$replacement.offset
        $length = [int]$replacement.length
        $lineEnding = Get-LocalLineEnding $originalBytes $offset $length
        $replacementBytes = Convert-ReplacementTextToBytes $replacement.InnerText $lineEnding
        $originalSegment = Get-BytesSlice $originalBytes $offset $length
        if ((Convert-BytesToNormalizedText -Bytes $originalSegment) -eq (Convert-BytesToNormalizedText -Bytes $replacementBytes)) {
            continue
        }
        $formattedBytes = Set-BytesRange $formattedBytes $offset $length $replacementBytes
    }

    return ,$formattedBytes
}

$repoRoot = (& git rev-parse --show-toplevel).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($repoRoot)) {
    Exit-WithMessage "Could not determine repository root. Run this from inside a Git checkout." 2
}
Set-Location $repoRoot

$clangFormat = Resolve-CommandPath @(
    $env:CLANG_FORMAT_BIN,
    "clang-format.exe",
    "clang-format",
    "C:\Program Files\LLVM\bin\clang-format.exe",
    "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe",
    "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\bin\clang-format.exe",
    "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Tools\Llvm\x64\bin\clang-format.exe",
    "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Tools\Llvm\x64\bin\clang-format.exe",
    "C:\Program Files\Microsoft Visual Studio\2026\Community\VC\Tools\Llvm\x64\bin\clang-format.exe",
    "C:\Program Files\Microsoft Visual Studio\2026\Professional\VC\Tools\Llvm\x64\bin\clang-format.exe",
    "C:\Program Files\Microsoft Visual Studio\2026\Enterprise\VC\Tools\Llvm\x64\bin\clang-format.exe"
)

if (-not $clangFormat) {
    Write-Host "clang-format.exe was not found."
    Write-Host ""
    Write-Host "Install Visual Studio's `"C++ Clang tools for Windows`" component, or install LLVM."
    Write-Host "To use a specific binary, set CLANG_FORMAT_BIN before running this script."
    exit 2
}

Write-Host "Found clang-format: $clangFormat"
& $clangFormat --version
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if ([string]::IsNullOrWhiteSpace($BaseRef)) {
    $BaseRef = $env:CLANG_FORMAT_BASE
}
if ([string]::IsNullOrWhiteSpace($BaseRef)) {
    & git rev-parse --verify origin/main *> $null
    if ($LASTEXITCODE -eq 0) {
        $BaseRef = "origin/main"
    }
    else {
        $BaseRef = "HEAD~1"
    }
}

& git rev-parse --verify $BaseRef *> $null
if ($LASTEXITCODE -ne 0) {
    Exit-WithMessage "Could not resolve clang-format base ref: $BaseRef" 2
}

$extensions = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
@(".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hxx", ".hh") | ForEach-Object { [void]$extensions.Add($_) }

# The extension of a path as git prints it. A diff header ends a path that holds a space with a tab, and git quotes a
# path with unusual characters; `GetExtension` refuses both as characters no Windows path may hold, which stopped the
# whole check at the first such file even when it was not a C or C++ source.
function Get-PathExtension([string]$path) {
    $bare = $path.TrimEnd("`t").Trim('"')
    if ($bare -match '(\.[^./\\]+)$') {
        return $Matches[1]
    }
    return ""
}

$rangesByFile = [ordered]@{}
$currentFile = $null
# The base is compared with the working tree, not with HEAD. `git diff $BaseRef HEAD` compares
# two commits, so a run made before the work is committed - which is when a check that gates a
# handoff is most likely to be run - sees an empty change set and reports a pass over nothing.
# That is not hypothetical: a change was once recorded as formatted while five of its six new
# files were not, and the Linux helper found 55 violations in them afterwards.
$diffLines = & git diff --unified=0 --no-ext-diff --diff-filter=ACMRTUXB $BaseRef -- src
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

foreach ($line in $diffLines) {
    if ($line -match '^\+\+\+ b/(.+)$') {
        $candidate = $Matches[1].TrimEnd("`t")
        $extension = Get-PathExtension $candidate
        if ($candidate.StartsWith("src/") -and $extensions.Contains($extension)) {
            $currentFile = $candidate
            if (-not $rangesByFile.Contains($currentFile)) {
                $rangesByFile[$currentFile] = [System.Collections.Generic.List[string]]::new()
            }
        }
        else {
            $currentFile = $null
        }
        continue
    }

    if ($null -ne $currentFile -and $line -match '^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@') {
        $start = [int]$Matches[1]
        $countText = $Matches[2]
        $count = if ([string]::IsNullOrWhiteSpace($countText)) { 1 } else { [int]$countText }
        if ($count -le 0) {
            continue
        }

        $end = $start + $count - 1
        [void]$rangesByFile[$currentFile].Add("$start`:$end")
    }
}

# A file Git has never seen appears in no diff at all, so the working-tree comparison above
# still cannot see a source file that has just been written. New files are the ones most worth
# checking and the ones a pre-commit run is most likely to have, so they are the other half of
# the fix rather than a refinement of it. The Linux helper lists them the same way, and the
# whole of such a file is its changed lines.
$untrackedFiles = @(& git ls-files --others --exclude-standard -- src)
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

foreach ($candidate in $untrackedFiles) {
    if ([string]::IsNullOrWhiteSpace($candidate)) {
        continue
    }

    $extension = Get-PathExtension $candidate
    if (-not ($candidate.StartsWith("src/") -and $extensions.Contains($extension))) {
        continue
    }

    if ($rangesByFile.Contains($candidate)) {
        continue
    }

    $untrackedPath = Join-Path $repoRoot ($candidate -replace '/', [System.IO.Path]::DirectorySeparatorChar)
    if (-not (Test-Path -LiteralPath $untrackedPath -PathType Leaf)) {
        continue
    }

    $lineCount = ([System.IO.File]::ReadAllLines($untrackedPath)).Length
    if ($lineCount -le 0) {
        continue
    }

    $rangesByFile[$candidate] = [System.Collections.Generic.List[string]]::new()
    [void]$rangesByFile[$candidate].Add("1:$lineCount")
}

if ($rangesByFile.Count -eq 0) {
    Write-Host "No changed C/C++ lines under src against $BaseRef."
    exit 0
}

$failed = $false
foreach ($entry in $rangesByFile.GetEnumerator()) {
    if ($entry.Value.Count -eq 0) {
        continue
    }

    $path = Join-Path $repoRoot ($entry.Key -replace '/', [System.IO.Path]::DirectorySeparatorChar)
    $lineArgs = @()
    foreach ($range in $entry.Value) {
        $lineArgs += "--lines=$range"
    }

    if ($Mode -eq "Format") {
        Write-Host "Formatting changed lines in $($entry.Key)"
        $originalBytes = [System.IO.File]::ReadAllBytes($path)
        $formattedBytes = Get-FormattedBytes $path $lineArgs
        if (-not (Test-ByteArrayEqual $originalBytes $formattedBytes)) {
            [System.IO.File]::WriteAllBytes($path, $formattedBytes)
        }
    }
    else {
        Write-Host "Checking changed lines in $($entry.Key)"
        $originalBytes = [System.IO.File]::ReadAllBytes($path)
        $formattedBytes = Get-FormattedBytes $path $lineArgs
        if (-not (Test-ByteArrayEqual $originalBytes $formattedBytes)) {
            Write-Host "Formatting differences found in $($entry.Key)"
            $failed = $true
        }
    }
}

if ($failed) {
    Write-Host ""
    Write-Host "clang-format found formatting differences in changed C/C++ lines."
    Write-Host "Run: build\scripts\msvc\format_changed.bat $BaseRef"
    exit 1
}

if ($Mode -eq "Check") {
    Write-Host "clang-format check passed for changed C/C++ lines against $BaseRef."
}
else {
    Write-Host "Formatted changed C/C++ lines against $BaseRef."
}
