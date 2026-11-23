<#
.SYNOPSIS
Captures the application's window for the user guide's pictures, the same way every time.

.DESCRIPTION
A page of the guide opens with a picture of what it describes. This script takes that picture from the application
itself: it starts a deployment's application with a storage root of its own, so nobody's settings are in the
picture and none are touched - the dark theme, the default preset and the Compact density a new profile starts
with, at 100 % and in the en_US locale; gives the main window a fixed place and size, 1440 x 900 unless told
otherwise; opens the images the pictures need through the application's own File > Open and the system's file
dialog; then, for each capture asked for, opens the process from the menu a person would use - the menu bar the
window shows on Alt - waits until what it opened has stopped changing - its preview has landed - and saves it as a
PNG. A process opens as a task, a page of the main window: its picture is the task, the canvas and the column,
cropped to them; a dialog is its own window. It closes each task as a person does and stops the application when it
is done.

The picture is taken with PrintWindow, which reads the window's own content: another window in front of it does not
get into the picture, and the pointer is never in it. The pointer is moved out of the frame while a picture is
taken, because what it hovers over is drawn highlighted, and put back afterwards. The application is run without
display scaling and in the en_US locale, so a picture is at 100 % and writes its numbers with a decimal point, as
the guide does, whatever the machine is set to.

-Display <mode> chooses how the opened image is shown before anything is captured - `Autostretch` for a linear
master, which is nearly black without it - through the display mode of the floating toolbar, and waits until the
view has redrawn.

What is captured is named in one of two ways:

  -Page <page.md>            a process page of the guide: the menu path is the page's `menu`, the picture is named
                             after the page (`gamma-correction.md` gives `gamma-correction.png`)
  -Capture "<name>=<menu>"   any menu path, such as "curves=Process > Intensity Transformations > Curves
                             Transformation"; the picture is `<name>.png`: a task, cropped as -Crop says, or the
                             dialog the menu item opens
  -Capture "<name>=@<button>" a button of the main window, by the name it shows - a button of the floating
                             toolbar, such as "settings=@Autostretch Settings" - for a dialog no menu opens

-Crop says what of a task the picture shows: `task`, its page (the default: the header, the canvas and the column);
`column`, the column alone; `canvas`, the canvas alone; `view`, the image of the image view the main window shows,
without the margins beside it; `window`, the whole main window; or the object name of any part of the main window,
such as `observatoryRightColumn`. -Float floats the task in a window of its own, as its
header's button does, and takes that window instead.

-MainWindow <name> adds a picture of the main window as it is when the captures are done, cropped to the part
-MainCrop names by its object name - `observatoryRightColumn` for the readout, `observatoryRail` for the rail - or
whole without it.

-MenuShot "<name>=<menu path>" adds a picture of the main window with the menus of the path open and the pointer on
its last item, such as "convert=Image > Convert Format > Convert to unsigned 16 bit". A window's own picture leaves out
the menus it opened, so each open menu is drawn by itself and laid over the main window's picture where it stands; the
screen is not read, and the picture is the same with the session's screen locked.

A task is captured as it opens unless -Set says what to do in it first:

  -Set "<control>=<value>[;<control>[=<value>]...]"
                             sets controls of each task or dialog before its picture is taken, in the order given. A
                             control is named by its object name, as the window's inventory under
                             docs/user-guide/data/ui records it (`shadowsValue`), or by the name it shows (`Apply`).
                             A spin box takes a number written with a decimal point, a combo box one of its choices,
                             a check box or a checkable button `On` or `Off`. A control named without a value is
                             pressed. `<control>@<x>,<y>` clicks in a control the application draws itself - a curve,
                             a plot - at a place given in percent of its width and height from its top left corner,
                             and `<control>@<x>,<y>><x>,<y>` drags from the first place to the second.
                             `<control>?<path>` presses a button that opens the system's file or folder dialog,
                             answers the dialog with the path and waits for what is then loaded. `<control>!`
                             presses a button that starts a run and waits, up to -RunTimeoutSeconds, until the
                             button is enabled again. `<control>^<reply>` presses a button and answers the message
                             it then shows - the first new window that holds a button of that name - with that
                             button. `%wait=<title>` waits, up to -RunTimeoutSeconds, for a message whose title
                             starts with <title> and answers it with OK; one titled Failed stops the capture.
                             `%until=<control>` waits, up to -RunTimeoutSeconds, until the control is shown and
                             enabled: Image Integration's btnOpenReport once its run has ended.
                             `%keys=<key>[,<key>...]` posts keys to the window - Tab, Down, Return and the
                             like - and `%type=<text>` types a text into what has the keyboard: what a dialog too
                             large for UI Automation, Settings, takes while it is modal.
                             `%shot=<name>` saves a picture of the main window as <name>.png while the task is still
                             open - what a task draws over the image is there only then - and `%shot=<name>@<part>`
                             crops it to a part, as -Crop does.
  -List                      prints the controls of each task or dialog it captures - type, object name and name -
                             which is what -Set can name.
  -Prepare "<menu path>|<control>=<value>[;...]"
                             gives the image the state the captures need, before any of them: opens that process,
                             sets its controls as -Set does, applies it - Apply, or OK where there is none, unless its
                             last change already ran it (`OK!`) - and closes it. A page of a process that works on a
                             stretched image is captured on the linear master after the stretch its own worked
                             example names, so the guide's data stays one file. Several, separated by `||`, are run
                             in the order given, and an image a preparation makes - the starless image of Starnet++ -
                             is there for the captures. "@<button>|<control>=<value>" opens a dialog by a button of
                             the main window instead - "@Autostretch Settings|linkedChannels=On" - whose changes act
                             at once, and closes it.

-View "<menu path>[;<menu path>...]" chooses menu items that act on the active image or the main window without a
window of their own - "Image > View > Zoom 100%", "View > Session Console", or `@<button>` for a button of the
main window such as "@Clear Session" or an item of the rail such as "@Integration", `%type=<text>`, which types into the
popup or the window that has the keyboard, or `%middle`, which puts the main
window's scroll bars at their middle so a
view zoomed to 100 % shows the middle of its image - once the preparations are done and before the first capture,
and waits until the view has redrawn. `%display=<mode>` chooses the active view's display mode, and
`%shot=<name>[@<part>]` saves the main window's picture there and then.

-Storage <folder> names the storage root instead of a new temporary one, and keeps it afterwards. It must be a new
folder, unless -ReuseStorage says it is one a previous run kept: the profile then starts with what that run left in
it, such as the path of an external tool - StarNet2 - which a new profile does not know and which only the Settings
dialog sets.

-Apply takes a before-and-after pair of the image the task works on, from the image's own view: before the task
opens it saves the upper two thirds of the view, clear of its floating toolbar, as `<name>-before.png`; once the
task's picture is saved it presses the task's Apply - or its OK - waits for the run to end, closes the task, and
saves the same part of the view as `<name>-after.png`. Both show the image under the display mode it was opened in,
at the same place and zoom - -View's "Image > View > Zoom 100%;%middle" for a process judged by its detail. A run
whose Apply changes nothing ends with an error. A pair of two images - a light and the master a run made of it - is taken with
`%shot=<name>@<part>` in -View and -AfterView instead: -AfterView "<actions>", with -View's syntax, is done once the
captures are done, before -MainWindow.

What a task's run leaves on the screen - the message that it is complete - is dismissed before the task is closed,
and an image it opened stays for -MainWindow, shown as -Display says.

A menu item that opens neither a task nor a window of its own - a submenu, an action that acts at once - ends the run
with an error that names it. So does a page's process that the application answers with a message instead - it
refuses the image: the script answers the message with Return and stops.

-WhatIf lists what the run would capture, picture by picture, and starts nothing.

The recipes. The guide's pictures are described once, in docs/user-guide/captures/: a file per group of pages, each
recipe the picture it makes, its page, the data it opens by role, what the picture shows and this script's options
for it. -Recipes <folder or file> runs them instead of one capture, each in a run of its own, the application started
anew; -Only <pattern>[,...] keeps the recipes whose picture or page matches; -Data "<role>=<path>[;...]" gives the
files a role names, which no recipe states; -Optimize <python> prepares each capture with optimize_guide_image.py
into the docs folder and writes the provenance rows it prints to <Output>\provenance.md, with -Commit naming the
application's commit. -Recipes with -WhatIf lists the pictures the recipes make, one a line, which is the inventory's
list of the pictures to take.

The PNGs are the captures as taken. build/scripts/tools/optimize_guide_image.py turns one into the file a page
shows, inside the guide's limits, and prints its row for the media provenance page. docs/user-guide/SCREENSHOTS.md
has the whole recipe.

Usage (Windows PowerShell 5.1, which carries UI Automation):
    powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\tools\capture_guide_screenshots.ps1
        -Application bin\msvc\release\AfterNight.exe -Output <folder for the PNGs> [-Open <image>[,<image>...]]
        [-Display <mode>] [-Page <page.md>[,...]] [-Capture "<name>=<menu path>[;...]"] [-Crop <part>] [-Float]
        [-MainWindow <name> [-MainCrop <part>]] [-Set "<control>=<value>[;...]"]
        [-Prepare "<menu path>|<control>=<value>[;...]"] [-View "<menu path>[;...]"] [-AfterView "<actions>"]
        [-Apply] [-List] [-WhatIf] [-Storage <new folder> | -Storage <kept folder> -ReuseStorage]
        [-Guide docs\user-guide] [-Left 80] [-Top 60] [-Width 1440] [-Height 900] [-SettleSeconds 20]
        [-OpenTimeoutSeconds 180] [-RunTimeoutSeconds 3600] [-OpenAnswer No] [-HoldSeconds 0]
    ... -File build\scripts\tools\capture_guide_screenshots.ps1 -Recipes docs\user-guide\captures
        [-Only <pattern>[,...]] [-WhatIf] -Application <exe> -Output <folder> -Data "<role>=<path>[;...]"
        [-Optimize <python> -Commit <commit>]

-OpenAnswer answers the question an opened file asks - "Open CFA file", whether to debayer a raw color-filter
frame on opening - with the button of that name. -HoldSeconds waits that long before a picture, for a preview that
takes long and shows no change while it works.

It exits 0 when every capture was saved, and 1, having stopped the application, when one was not.
#>
param(
    [string]$Application = '',
    [string]$Output = '',
    [string[]]$Open = @(),
    [string]$Display = '',
    [string[]]$Page = @(),
    [string[]]$Capture = @(),
    [string]$Crop = '',
    [switch]$Float,
    [string]$MainWindow = '',
    [string]$MainCrop = '',
    [string]$MenuShot = '',
    [string[]]$Set = @(),
    [string[]]$Prepare = @(),
    [string[]]$View = @(),
    [string[]]$AfterView = @(),
    [switch]$Apply,
    [switch]$List,
    [switch]$WhatIf,
    [string]$Recipes = '',
    [string[]]$Only = @(),
    [string[]]$Data = @(),
    [string]$Optimize = '',
    [string]$Commit = '',
    [string]$Storage = '',
    [switch]$ReuseStorage,
    [string]$Guide = '',
    [int]$Left = 80,
    [int]$Top = 60,
    [int]$Width = 1440,
    [int]$Height = 900,
    [int]$SettleSeconds = 20,
    [int]$OpenTimeoutSeconds = 180,
    [int]$RunTimeoutSeconds = 3600,
    [string]$OpenAnswer = '',
    [int]$HoldSeconds = 0
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path (Split-Path (Split-Path $PSScriptRoot))
if (-not $Guide) { $Guide = Join-Path $repository 'docs\user-guide' }

# Called with -File, a list arrives as one text: the pictures -Only keeps are told apart by commas, data by
# semicolons. No variable is named after a parameter in other letters' case: PowerShell's names ignore case, and a
# parameter keeps its type, so `$picture` would be the text `-Picture` is.
$Only = @($Only | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$Data = @($Data | ForEach-Object { $_ -split ';' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })

# The recipes of the guide's pictures: a file per group of pages under docs/user-guide/captures, and roles.json, what
# each role of data is. A recipe states its picture, its page, the roles of the data it opens, what the picture shows
# and this script's options; one that takes a pair (-Apply) says what each half shows as `before` and `after`.
function Read-Recipes([string]$from) {
    $files = @()
    if (Test-Path -LiteralPath $from -PathType Container) { $files = @(Get-ChildItem -LiteralPath $from -Filter '*.json' | Sort-Object Name) }
    elseif (Test-Path -LiteralPath $from -PathType Leaf) { $files = @(Get-Item -LiteralPath $from) }
    else { throw "-Recipes names no file or folder: $from" }
    $roles = @{}
    $found = New-Object System.Collections.Generic.List[object]
    foreach ($file in $files) {
        $json = [System.IO.File]::ReadAllText($file.FullName, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
        $keys = @($json.PSObject.Properties | ForEach-Object { $_.Name })
        if ($keys -contains 'roles') {
            foreach ($role in $json.roles.PSObject.Properties) { $roles[$role.Name] = [string]$role.Value }
            continue
        }
        foreach ($recipe in @($json.captures)) {
            $stated = @($recipe.PSObject.Properties | ForEach-Object { $_.Name })
            foreach ($key in @('picture', 'page', 'data', 'shows', 'options')) {
                if ($stated -notcontains $key) { throw "$($file.Name): every recipe states picture, page, data, shows and options; $($recipe.picture) does not state $key" }
            }
            $options = @($recipe.options | ForEach-Object { [string]$_ })
            $pair = $options -contains '-Apply'
            if ($pair -and ($stated -notcontains 'before' -or $stated -notcontains 'after')) { throw "$($file.Name): $($recipe.picture) takes a pair, and says what its halves show as before and after" }
            $found.Add([pscustomobject]@{
                group = [string]$json.group; file = $file.Name; picture = [string]$recipe.picture; page = [string]$recipe.page
                data = @($recipe.data | ForEach-Object { [string]$_ } | Where-Object { $_ }); shows = [string]$recipe.shows
                options = $options; pair = $pair; before = [string]$recipe.before; after = [string]$recipe.after
            })
        }
    }
    return ,@($found, $roles)
}

# The pictures a recipe makes: its own, and the halves of its pair.
function Get-RecipePictures($recipe) {
    $made = @($recipe.picture)
    if ($recipe.pair) {
        $made += ($recipe.picture -replace '\.(webp|png|jpg)$', '-before.$1')
        $made += ($recipe.picture -replace '\.(webp|png|jpg)$', '-after.$1')
    }
    return $made
}

if ($Recipes) {
    $read = Read-Recipes $Recipes
    $all = $read[0]
    $roles = $read[1]
    $chosen = @($all | Where-Object {
        $recipe = $_
        $Only.Count -eq 0 -or @($Only | Where-Object { $recipe.picture -like "*$_*" -or $recipe.page -like "*$_*" }).Count -gt 0
    })
    if ($WhatIf) {
        $chosen | ForEach-Object { Get-RecipePictures $_ } | Sort-Object -Unique | ForEach-Object { Write-Output $_ }
        exit 0
    }
    if (-not $Application -or -not $Output) { throw '-Recipes runs the application: name -Application and -Output' }
    $paths = @{}
    foreach ($given in $Data) {
        $at = $given.IndexOf('=')
        if ($at -lt 1) { throw "-Data takes ""<role>=<path>"", not ""$given""" }
        $paths[$given.Substring(0, $at).Trim()] = $given.Substring($at + 1).Trim()
    }
    New-Item -ItemType Directory -Force -Path $Output | Out-Null
    $rows = Join-Path $Output 'provenance.md'
    $failedRecipes = New-Object System.Collections.Generic.List[string]
    foreach ($recipe in $chosen) {
        # The files the recipe opens: its roles' files. A role that names a folder - a set of frames - is not opened;
        # the recipe's options reach it by its placeholder.
        $files = @()
        foreach ($role in $recipe.data) {
            if (-not $paths.ContainsKey($role)) { throw "-Data gives no file for the role '$role' that $($recipe.picture) opens" }
            if (-not (Test-Path -LiteralPath $paths[$role] -PathType Container)) { $files += $paths[$role] }
        }
        # `{<role>}` in an option is the role's path, and `{<role>:<name>}` a file or folder inside it: a set's lights,
        # the folder a project or a run writes to (`{work}`), which no recipe states as a path.
        $options = @()
        foreach ($option in $recipe.options) {
            $options += [regex]::Replace($option, '\{([A-Za-z0-9_-]+)(?::([^}]+))?\}', {
                param($found)
                $role = $found.Groups[1].Value
                if (-not $paths.ContainsKey($role)) { throw "-Data gives no path for the role '$role' that $($recipe.picture) names" }
                if ($found.Groups[2].Success) { return (Join-Path $paths[$role] $found.Groups[2].Value) }
                return $paths[$role]
            })
        }
        $name = [System.IO.Path]::GetFileNameWithoutExtension($recipe.picture)
        $folder = Join-Path $Output $name
        $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath, '-Application', $Application,
                       '-Output', $folder, '-Guide', $Guide, '-SettleSeconds', $SettleSeconds,
                       '-OpenTimeoutSeconds', $OpenTimeoutSeconds, '-RunTimeoutSeconds', $RunTimeoutSeconds)
        if ($files.Count -gt 0) { $arguments += @('-Open', ($files -join ',')) }
        # Windows PowerShell hands an argument to a program without escaping the quotes inside it: a file dialog's
        # answer of several files, `"a.fits" "b.fits"`, would lose them.
        $arguments += @($options | ForEach-Object { $_.Replace('"', '\"') })
        # The application keeps one instance: the last run's must be gone before the next starts.
        $deployed = (Resolve-Path -LiteralPath $Application).Path
        $gone = [System.Diagnostics.Stopwatch]::StartNew()
        while (@(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $deployed }).Count -gt 0 -and $gone.ElapsedMilliseconds -lt 30000) {
            Start-Sleep -Milliseconds 500
        }
        Write-Output "=== $($recipe.picture)"
        & powershell.exe @arguments
        if ($LASTEXITCODE -ne 0) { $failedRecipes.Add($recipe.picture); continue }
        if (-not $Optimize) { continue }
        $described = @($recipe.data | ForEach-Object { if ($roles.ContainsKey($_)) { $roles[$_] } else { $_ } }) -join '; '
        if (-not $described) { $described = 'No image' }
        $halves = @(@{ picture = $recipe.picture; shows = $recipe.shows; png = "$name.png" })
        if ($recipe.pair) {
            $halves += @{ picture = ($recipe.picture -replace '\.(webp|png|jpg)$', '-before.$1'); shows = $recipe.before; png = "$name-before.png" }
            $halves += @{ picture = ($recipe.picture -replace '\.(webp|png|jpg)$', '-after.$1'); shows = $recipe.after; png = "$name-after.png" }
        }
        foreach ($half in $halves) {
            $png = Join-Path $folder $half.png
            if (-not (Test-Path -LiteralPath $png)) { $failedRecipes.Add($half.picture); continue }
            # A picture of the guide is named from its docs folder; the README's, from the repository (`docs/readme/`).
            $base = Join-Path $Guide 'docs\en'
            if ($half.picture.StartsWith('docs/')) { $base = $repository }
            $target = Join-Path $base ($half.picture -replace '\.(webp|png|jpg)$', '' -replace '/', '\')
            $optimizer = Join-Path $PSScriptRoot 'optimize_guide_image.py'
            $row = & $Optimize $optimizer $png --to $target --shows $half.shows --data $described --commit $Commit
            if ($LASTEXITCODE -ne 0) { $failedRecipes.Add($half.picture); continue }
            Add-Content -LiteralPath $rows -Value (@("### $($recipe.group): $($half.picture)") + @($row)) -Encoding UTF8
        }
    }
    if ($failedRecipes.Count -gt 0) {
        Write-Output "not made: $($failedRecipes -join ', ')"
        exit 1
    }
    exit 0
}
if (-not $Application -and -not $WhatIf) { throw 'name the application to start with -Application' }
if (-not $Output -and -not $WhatIf) { throw 'name the folder the pictures go to with -Output' }
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing

$source = @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Automation;

public static class AnCaptureNative
{
    public delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr dialog, int id);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr hwnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessageTimeout(IntPtr hwnd, uint message, IntPtr wParam, string lParam, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point point);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool ScreenToClient(IntPtr hwnd, ref Point point);
    [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr hwnd, int index);
    [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr hwnd, int attribute, out Rect value, int size);
}

/// One top-level window of the application.
public sealed class AnShownWindow
{
    public IntPtr Handle;
    public string Title;
    public string ClassName;
}

/// Drives the application's windows and takes their pictures.
public sealed class AnScreenshots
{
    private readonly Process m_process;
    private readonly uint m_pid;
    private readonly Stopwatch m_clock = Stopwatch.StartNew();

    public AnScreenshots(Process process)
    {
        m_process = process;
        m_pid = (uint)process.Id;
    }

    public void Note(string text)
    {
        Console.WriteLine(string.Format("{0,8:F1} s  {1}", m_clock.ElapsedMilliseconds / 1000.0, text));
    }

    public void CheckAlive()
    {
        if (m_process.HasExited) throw new InvalidOperationException("the application exited with code " + m_process.ExitCode);
    }

    // ---- Top-level windows, read through the window manager.

    public List<AnShownWindow> Windows()
    {
        List<AnShownWindow> found = new List<AnShownWindow>();
        uint pid = m_pid;
        AnCaptureNative.EnumWindows(delegate(IntPtr hwnd, IntPtr unused)
        {
            uint owner;
            AnCaptureNative.GetWindowThreadProcessId(hwnd, out owner);
            if (owner != pid || !AnCaptureNative.IsWindowVisible(hwnd)) return true;
            StringBuilder title = new StringBuilder(512);
            AnCaptureNative.GetWindowText(hwnd, title, 512);
            StringBuilder cls = new StringBuilder(256);
            AnCaptureNative.GetClassName(hwnd, cls, 256);
            AnShownWindow window = new AnShownWindow();
            window.Handle = hwnd;
            window.Title = title.ToString();
            window.ClassName = cls.ToString();
            found.Add(window);
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public bool IsShown(IntPtr handle)
    {
        foreach (AnShownWindow window in Windows())
        {
            if (window.Handle == handle) return true;
        }
        return false;
    }

    /// Whether a window's class is one of the toolkit's windows a person works in, with or without an icon of its
    /// own (a tool window has none), and not one of its popups - a menu, a combo's list - or a tooltip.
    public static bool IsWorkWindowClass(string className)
    {
        return className.Contains("QWindow") && !className.Contains("Popup") && !className.Contains("ToolTip");
    }

    /// A window that was not among `before` and is one a person works in: it has a title, and is neither the
    /// system's dialog nor one of the toolkit's popups - a menu, a tooltip, a combo's list.
    public AnShownWindow NewWindow(ICollection<IntPtr> before)
    {
        foreach (AnShownWindow window in Windows())
        {
            if (before.Contains(window.Handle) || window.Title.Length == 0) continue;
            if (window.ClassName == "#32770" || !IsWorkWindowClass(window.ClassName)) continue;
            return window;
        }
        return null;
    }

    /// Whether a window has a minimize button, as every process window has and a message box or a dialog has not.
    /// The window's own style says so; UI Automation cannot be asked while a modal box is open, because the
    /// invocation of the menu item that raised it has not returned.
    public bool HasMinimizeButton(IntPtr handle)
    {
        return (AnCaptureNative.GetWindowLong(handle, -16) & 0x00020000) != 0;
    }

    /// The system's file dialog, when one is open.
    public AnShownWindow NativeDialog()
    {
        foreach (AnShownWindow window in Windows())
        {
            if (window.ClassName == "#32770") return window;
        }
        return null;
    }

    /// Answers the system's file dialog with a path: its name box is given the path and the dialog is accepted.
    public void AnswerNativeDialog(string path, int timeoutMs)
    {
        Stopwatch clock = Stopwatch.StartNew();
        AnShownWindow dialog = null;
        while (dialog == null && clock.ElapsedMilliseconds < timeoutMs)
        {
            dialog = NativeDialog();
            if (dialog == null) { CheckAlive(); Thread.Sleep(50); }
        }
        if (dialog == null) throw new TimeoutException("no file dialog opened");
        Thread.Sleep(300);
        IntPtr edit = AnCaptureNative.GetDlgItem(dialog.Handle, 1152);
        if (edit == IntPtr.Zero) edit = AnCaptureNative.GetDlgItem(dialog.Handle, 1148);
        // A Save dialog keeps its name box deeper down: an edit inside the combo box of control 1001.
        if (edit == IntPtr.Zero)
        {
            IntPtr combo = IntPtr.Zero;
            AnCaptureNative.EnumChildWindows(dialog.Handle, delegate (IntPtr child, IntPtr unused)
            {
                if (AnCaptureNative.GetDlgCtrlID(child) == 1001) { combo = child; return false; }
                return true;
            }, IntPtr.Zero);
            if (combo != IntPtr.Zero)
            {
                edit = AnCaptureNative.FindWindowEx(combo, IntPtr.Zero, "Edit", null);
                if (edit == IntPtr.Zero) edit = combo;
            }
        }
        if (edit == IntPtr.Zero) throw new InvalidOperationException("the file dialog has no name box");
        IntPtr ignored;
        AnCaptureNative.SendMessageTimeout(edit, 0x000C, IntPtr.Zero, path, 2, 5000, out ignored);
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            AnCaptureNative.PostMessage(dialog.Handle, 0x0111, (IntPtr)1, IntPtr.Zero);
            Stopwatch wait = Stopwatch.StartNew();
            while (wait.ElapsedMilliseconds < 2000)
            {
                if (!IsShown(dialog.Handle)) return;
                Thread.Sleep(50);
            }
        }
        throw new TimeoutException("the file dialog did not accept " + path);
    }

    /// Presses and releases a key in a window, as a posted message: the window need not be in front.
    public void PostKey(IntPtr handle, int virtualKey)
    {
        AnCaptureNative.PostMessage(handle, 0x0100, (IntPtr)virtualKey, IntPtr.Zero);
        AnCaptureNative.PostMessage(handle, 0x0101, (IntPtr)virtualKey, (IntPtr)unchecked((int)0xC0000001));
    }

    /// Types a text into a window, a character at a time, as posted characters.
    public void PostText(IntPtr handle, string text)
    {
        foreach (char character in text)
        {
            AnCaptureNative.PostMessage(handle, 0x0102, (IntPtr)character, IntPtr.Zero);
            Thread.Sleep(40);
        }
    }

    public void Close(IntPtr handle)
    {
        AnCaptureNative.PostMessage(handle, 0x0010, IntPtr.Zero, IntPtr.Zero);
    }

    /// Presses and releases the left button at a place on the screen that lies in a window, as posted messages.
    public void PostClick(IntPtr handle, int screenX, int screenY)
    {
        AnCaptureNative.Point point;
        point.X = screenX;
        point.Y = screenY;
        AnCaptureNative.ScreenToClient(handle, ref point);
        IntPtr where = (IntPtr)((point.Y << 16) | (point.X & 0xFFFF));
        AnCaptureNative.PostMessage(handle, 0x0201, (IntPtr)1, where);
        AnCaptureNative.PostMessage(handle, 0x0202, IntPtr.Zero, where);
    }

    /// Presses the left button at one place on the screen, moves to another with the button held and releases it
    /// there, as posted messages: what moves a point of a curve or a handle of a plot.
    public void PostDrag(IntPtr handle, int fromX, int fromY, int toX, int toY)
    {
        AnCaptureNative.Point from;
        from.X = fromX;
        from.Y = fromY;
        AnCaptureNative.ScreenToClient(handle, ref from);
        AnCaptureNative.Point to;
        to.X = toX;
        to.Y = toY;
        AnCaptureNative.ScreenToClient(handle, ref to);
        IntPtr start = (IntPtr)((from.Y << 16) | (from.X & 0xFFFF));
        IntPtr end = (IntPtr)((to.Y << 16) | (to.X & 0xFFFF));
        AnCaptureNative.PostMessage(handle, 0x0201, (IntPtr)1, start);
        Thread.Sleep(150);
        AnCaptureNative.PostMessage(handle, 0x0200, (IntPtr)1, end);
        Thread.Sleep(150);
        AnCaptureNative.PostMessage(handle, 0x0202, IntPtr.Zero, end);
    }

    /// Restores a window that opened maximized and gives it a place and a size, without bringing it to the front.
    public void Place(IntPtr handle, int left, int top, int width, int height)
    {
        AnCaptureNative.ShowWindow(handle, 4);
        AnCaptureNative.ShowWindow(handle, 9);
        AnCaptureNative.SetWindowPos(handle, IntPtr.Zero, left, top, width, height, 0x0004 | 0x0010);
    }

    /// The window's frame on the screen: what a person sees of it, without the margin the system keeps around a
    /// window for its shadow and its resize handles.
    public AnCaptureNative.Rect Frame(IntPtr handle)
    {
        AnCaptureNative.Rect frame;
        if (AnCaptureNative.DwmGetWindowAttribute(handle, 9, out frame, Marshal.SizeOf(typeof(AnCaptureNative.Rect))) != 0)
        {
            AnCaptureNative.GetWindowRect(handle, out frame);
        }
        return frame;
    }

    /// The window with the menus it opened drawn over it where they stand: each menu is its own window, which the
    /// window's own picture leaves out, so each is drawn by itself and laid over the window's, the lowest first. The
    /// screen is not read, so the picture is the same whether the session's screen is shown or locked.
    public Bitmap MenuPicture(IntPtr handle)
    {
        AnCaptureNative.Rect frame = Frame(handle);
        Bitmap picture = Picture(handle);
        List<AnShownWindow> menus = new List<AnShownWindow>();
        foreach (AnShownWindow window in Windows())
        {
            if (window.Handle != handle && window.ClassName.Contains("QWindowPopup")) menus.Add(window);
        }
        menus.Reverse();
        using (Graphics graphics = Graphics.FromImage(picture))
        {
            foreach (AnShownWindow menu in menus)
            {
                AnCaptureNative.Rect place = Frame(menu.Handle);
                using (Bitmap drawn = Picture(menu.Handle))
                {
                    graphics.DrawImageUnscaled(drawn, place.Left - frame.Left, place.Top - frame.Top);
                }
            }
        }
        return picture;
    }

    /// Rests the pointer on the middle of an element, so the toolkit draws it highlighted: the pointer is put there,
    /// and the menu window under it is told the pointer moved, which it is not told when the screen is locked.
    public void PointAt(AutomationElement element)
    {
        System.Windows.Rect box = element.Current.BoundingRectangle;
        int x = (int)(box.Left + box.Width / 2);
        int y = (int)(box.Top + box.Height / 2);
        AnCaptureNative.SetCursorPos(x, y);
        foreach (AnShownWindow window in Windows())
        {
            if (!window.ClassName.Contains("QWindowPopup")) continue;
            AnCaptureNative.Rect place;
            if (!AnCaptureNative.GetWindowRect(window.Handle, out place)) continue;
            if (x < place.Left || x >= place.Right || y < place.Top || y >= place.Bottom) continue;
            AnCaptureNative.Point point;
            point.X = x;
            point.Y = y;
            AnCaptureNative.ScreenToClient(window.Handle, ref point);
            AnCaptureNative.PostMessage(window.Handle, 0x0200, IntPtr.Zero, (IntPtr)((point.Y << 16) | (point.X & 0xFFFF)));
            break;
        }
    }

    /// The window as it draws itself, cropped to its frame.
    public Bitmap Picture(IntPtr handle)
    {
        AnCaptureNative.Rect whole;
        if (!AnCaptureNative.GetWindowRect(handle, out whole)) throw new InvalidOperationException("the window has gone");
        AnCaptureNative.Rect frame = Frame(handle);
        int width = whole.Right - whole.Left;
        int height = whole.Bottom - whole.Top;
        if (width <= 0 || height <= 0) throw new InvalidOperationException("the window has no size");
        using (Bitmap full = new Bitmap(width, height, PixelFormat.Format32bppArgb))
        {
            using (Graphics graphics = Graphics.FromImage(full))
            {
                IntPtr hdc = graphics.GetHdc();
                bool drawn = AnCaptureNative.PrintWindow(handle, hdc, 2);
                graphics.ReleaseHdc(hdc);
                if (!drawn) throw new InvalidOperationException("the window could not be drawn");
            }
            Rectangle crop = Rectangle.Intersect(new Rectangle(0, 0, width, height),
                new Rectangle(frame.Left - whole.Left, frame.Top - whole.Top, frame.Right - frame.Left, frame.Bottom - frame.Top));
            return full.Clone(crop, PixelFormat.Format24bppRgb);
        }
    }

    private static bool Same(Bitmap one, Bitmap other)
    {
        if (one.Width != other.Width || one.Height != other.Height) return false;
        Rectangle all = new Rectangle(0, 0, one.Width, one.Height);
        BitmapData first = one.LockBits(all, ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
        BitmapData second = other.LockBits(all, ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
        try
        {
            int length = Math.Abs(first.Stride) * first.Height;
            byte[] left = new byte[length];
            byte[] right = new byte[length];
            Marshal.Copy(first.Scan0, left, 0, length);
            Marshal.Copy(second.Scan0, right, 0, length);
            for (int index = 0; index < length; ++index)
            {
                if (left[index] != right[index]) return false;
            }
            return true;
        }
        finally
        {
            one.UnlockBits(first);
            other.UnlockBits(second);
        }
    }

    /// The window's picture once two taken a moment apart are the same - what it computes as it opens has been
    /// drawn - or, when it never rests, as it is when the time is up. `rested` says which.
    public Bitmap SettledPicture(IntPtr handle, int timeoutMs, out bool rested)
    {
        Stopwatch clock = Stopwatch.StartNew();
        Bitmap last = Picture(handle);
        int same = 0;
        while (clock.ElapsedMilliseconds < timeoutMs)
        {
            Thread.Sleep(700);
            CheckAlive();
            Bitmap next = Picture(handle);
            same = Same(last, next) ? same + 1 : 0;
            last.Dispose();
            last = next;
            if (same >= 2) { rested = true; return last; }
        }
        rested = false;
        return last;
    }

    /// Moves the pointer out of a window's frame, when it is inside; returns where it was, for PutPointerBack.
    public AnCaptureNative.Point MovePointerOut(IntPtr handle, out bool moved)
    {
        AnCaptureNative.Point at;
        AnCaptureNative.GetCursorPos(out at);
        AnCaptureNative.Rect frame = Frame(handle);
        moved = at.X >= frame.Left && at.X < frame.Right && at.Y >= frame.Top && at.Y < frame.Bottom;
        if (moved) AnCaptureNative.SetCursorPos(frame.Right + 40, frame.Top + 40);
        return at;
    }

    public void PutPointerBack(AnCaptureNative.Point at)
    {
        AnCaptureNative.SetCursorPos(at.X, at.Y);
    }

    // ---- The Observatory window: its parts, a task's page, and the scope a task's controls are looked for in.

    private readonly Dictionary<IntPtr, AutomationElement> m_scopes = new Dictionary<IntPtr, AutomationElement>();

    /// Looks for the controls of `window` under `scope` instead of the whole window: a task's page in the main
    /// window, which is walked whole quickly, where the main window with a large image open is not.
    public void SetScope(IntPtr window, AutomationElement scope)
    {
        m_scopes[window] = scope;
    }

    public void ClearScope(IntPtr window)
    {
        m_scopes.Remove(window);
    }

    /// Where the controls of a window are looked for: its scope, or the whole window.
    private AutomationElement Root(IntPtr window)
    {
        AutomationElement scope;
        if (m_scopes.TryGetValue(window, out scope)) return scope;
        return AutomationElement.FromHandle(window);
    }

    /// The element under `scope` whose object name is `name`, walked a level at a time - the main window with a large
    /// image open is too large to be asked for all its descendants at once. A name ending in "." is the beginning of
    /// an object name that goes on: "observatoryTaskPage." is any task's page. Only what is shown is looked into.
    /// Null when there is none.
    public AutomationElement ByObject(AutomationElement scope, string name, int depth)
    {
        bool prefix = name.EndsWith(".");
        List<AutomationElement> level = new List<AutomationElement>();
        level.Add(scope);
        for (int at = 0; at <= depth && level.Count > 0; ++at)
        {
            List<AutomationElement> next = new List<AutomationElement>();
            foreach (AutomationElement element in level)
            {
                AutomationElement child = null;
                try { child = TreeWalker.ControlViewWalker.GetFirstChild(element); }
                catch (ElementNotAvailableException) { child = null; }
                while (child != null)
                {
                    try
                    {
                        string id = child.Current.AutomationId ?? "";
                        bool found = prefix ? id.Contains("." + name) : (id == name || id.EndsWith("." + name));
                        // An image view holds its picture and nothing of the window's own.
                        bool view = child.Current.ClassName == "ImageViewControl";
                        if (found && !child.Current.IsOffscreen) return child;
                        if (!view && !child.Current.IsOffscreen) next.Add(child);
                        child = TreeWalker.ControlViewWalker.GetNextSibling(child);
                    }
                    catch (ElementNotAvailableException) { break; }
                }
            }
            level = next;
        }
        return null;
    }

    /// The page of the task the main window shows, or null when it shows another place.
    public AutomationElement TaskPage(AutomationElement main)
    {
        AutomationElement centre = ByObject(main, "observatoryCentre", 3);
        if (centre == null) return null;
        AutomationElement child = null;
        try { child = TreeWalker.ControlViewWalker.GetFirstChild(centre); }
        catch (ElementNotAvailableException) { return null; }
        while (child != null)
        {
            try
            {
                if ((child.Current.AutomationId ?? "").Contains(".observatoryTaskPage.") && !child.Current.IsOffscreen) return child;
                child = TreeWalker.ControlViewWalker.GetNextSibling(child);
            }
            catch (ElementNotAvailableException) { return null; }
        }
        return null;
    }

    /// The identifier of the task page the main window shows; empty when it shows another place.
    public string TaskPageId(AutomationElement main)
    {
        AutomationElement page = TaskPage(main);
        try { return page == null ? "" : (page.Current.AutomationId ?? ""); }
        catch (ElementNotAvailableException) { return ""; }
    }

    /// Shows the menu bar, which the Observatory window keeps hidden until Alt is pressed: Alt is pressed until the
    /// bar's first menu is on the screen. A bar that shows already - the setting that keeps it - is left.
    public void ShowMenuBar(AutomationElement main)
    {
        IntPtr handle = (IntPtr)main.Current.NativeWindowHandle;
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            if (MenuBarShown(main)) return;
            AnCaptureNative.PostMessage(handle, 0x0104, (IntPtr)0x12, (IntPtr)0x20380001);
            Thread.Sleep(80);
            AnCaptureNative.PostMessage(handle, 0x0105, (IntPtr)0x12, (IntPtr)unchecked((int)0xC0380001));
            Thread.Sleep(600);
        }
        if (!MenuBarShown(main)) throw new InvalidOperationException("the menu bar did not show on Alt");
    }

    /// Whether the menu bar shows: its first menu has a place on the screen.
    private bool MenuBarShown(AutomationElement main)
    {
        try
        {
            AutomationElement bar = main.FindFirst(TreeScope.Children, new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.MenuBar));
            if (bar == null) return false;
            AutomationElement first = TreeWalker.ControlViewWalker.GetFirstChild(bar);
            if (first == null) return false;
            System.Windows.Rect place = first.Current.BoundingRectangle;
            return !first.Current.IsOffscreen && !place.IsEmpty && place.Width > 0 && place.Height > 0;
        }
        catch (ElementNotAvailableException) { return false; }
    }

    /// The part of a window's picture an element covers - the element's place on the screen, in the window's frame -
    /// the upper `fraction` of it. Null when the element has no place on the screen.
    public Bitmap Cropped(Bitmap picture, IntPtr window, AutomationElement element, double fraction)
    {
        System.Windows.Rect place = element.Current.BoundingRectangle;
        if (place.IsEmpty || place.Width < 1 || place.Height < 1) return null;
        AnCaptureNative.Rect frame = Frame(window);
        int height = (int)Math.Round(place.Height * fraction);
        Rectangle wanted = new Rectangle((int)place.X - frame.Left, (int)place.Y - frame.Top, (int)place.Width, height);
        Rectangle crop = Rectangle.Intersect(new Rectangle(0, 0, picture.Width, picture.Height), wanted);
        if (crop.Width < 1 || crop.Height < 1) return null;
        return picture.Clone(crop, PixelFormat.Format24bppRgb);
    }

    /// The part of a window's picture a place on the screen covers, the upper `fraction` of it; null when it covers
    /// nothing of the picture.
    public Bitmap CroppedTo(Bitmap picture, IntPtr window, System.Windows.Rect place, double fraction)
    {
        if (place.IsEmpty || place.Width < 1 || place.Height < 1) return null;
        AnCaptureNative.Rect frame = Frame(window);
        int height = (int)Math.Round(place.Height * fraction);
        Rectangle wanted = new Rectangle((int)place.X - frame.Left, (int)place.Y - frame.Top, (int)place.Width, height);
        Rectangle crop = Rectangle.Intersect(new Rectangle(0, 0, picture.Width, picture.Height), wanted);
        if (crop.Width < 1 || crop.Height < 1) return null;
        return picture.Clone(crop, PixelFormat.Format24bppRgb);
    }

    /// Where the image of the image view the main window shows lies on the screen: the picture inside the view, as
    /// much of it as the view shows - not the margins beside an image fitted to its view. Empty when no view shows.
    public System.Windows.Rect ViewImagePlace(AutomationElement main)
    {
        AutomationElement view = ByObject(main, "ImageViewControl", 14);
        if (view == null) return System.Windows.Rect.Empty;
        System.Windows.Rect shown = view.Current.BoundingRectangle;
        AutomationElement image = null;
        try
        {
            image = view.FindFirst(TreeScope.Descendants, new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.Image));
        }
        catch (ElementNotAvailableException) { image = null; }
        if (image == null) return shown;
        System.Windows.Rect drawn = image.Current.BoundingRectangle;
        drawn.Intersect(shown);
        return drawn.IsEmpty ? shown : drawn;
    }

    /// Closes the menus that are open, the last opened first, by Escape posted to each: no UI Automation call, which
    /// a modal dialog a menu item opened would hold up.
    public void CloseOpenMenus()
    {
        for (int pass = 0; pass < 6; ++pass)
        {
            AnShownWindow open = null;
            foreach (AnShownWindow window in Windows())
            {
                if (window.ClassName.Contains("QWindowPopup")) { open = window; break; }
            }
            if (open == null) return;
            PostKey(open.Handle, 0x1B);
            Thread.Sleep(200);
        }
    }

    /// A popup that was not among `before` and holds controls - a panel a button opens as a popup - or null. A menu
    /// is a popup too, which the menu paths open and close themselves.
    public AnShownWindow NewPopup(ICollection<IntPtr> before)
    {
        foreach (AnShownWindow window in Windows())
        {
            if (before.Contains(window.Handle) || !window.ClassName.Contains("QWindowPopup")) continue;
            return window;
        }
        return null;
    }

    /// Whether the application shows a popup - a menu, the command palette - which a window's own picture leaves out.
    public bool HasPopup()
    {
        foreach (AnShownWindow window in Windows())
        {
            if (window.ClassName.Contains("QWindowPopup")) return true;
        }
        return false;
    }

    /// Whether a control of a window is shown: on the screen, not hidden.
    public bool IsShownControl(IntPtr window, string name)
    {
        AutomationElement control = Control(window, name);
        try { return control != null && !control.Current.IsOffscreen; }
        catch (ElementNotAvailableException) { return false; }
    }

    /// Whether an image view shows: its floating toolbar's display mode is there.
    public bool HasDisplayMode(AutomationElement main)
    {
        try
        {
            return main.FindFirst(TreeScope.Descendants, new AndCondition(
                new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.CheckBox),
                new PropertyCondition(AutomationElement.NameProperty, "Linear"))) != null;
        }
        catch (ElementNotAvailableException) { return false; }
    }

    /// Chooses an image view's display mode by its segment in the floating toolbar - Linear or Autostretch - looked
    /// for by its type and its name, as a person sees it. False when no segment has that name.
    public bool ChooseDisplayMode(AutomationElement main, string mode)
    {
        AutomationElement segment = null;
        try
        {
            segment = main.FindFirst(TreeScope.Descendants, new AndCondition(
                new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.CheckBox),
                new PropertyCondition(AutomationElement.NameProperty, mode)));
        }
        catch (ElementNotAvailableException) { segment = null; }
        if (segment == null) return false;
        // The view follows the segment's click: a toggle through UI Automation checks it without one, and the mode
        // stays as it was. A segment already chosen is left.
        object pattern;
        if (segment.TryGetCurrentPattern(TogglePattern.Pattern, out pattern) &&
            ((TogglePattern)pattern).Current.ToggleState == ToggleState.On) return true;
        if (segment.TryGetCurrentPattern(InvokePattern.Pattern, out pattern)) { ((InvokePattern)pattern).Invoke(); return true; }
        System.Windows.Rect place = segment.Current.BoundingRectangle;
        if (place.IsEmpty) return false;
        PostClick((IntPtr)main.Current.NativeWindowHandle, (int)(place.X + place.Width / 2), (int)(place.Y + place.Height / 2));
        return true;
    }

    // ---- The application's menus, read through UI Automation.

    private Condition ProcessCondition()
    {
        return new PropertyCondition(AutomationElement.ProcessIdProperty, (int)m_pid);
    }

    public AutomationElement MainWindow(int timeoutMs)
    {
        Stopwatch clock = Stopwatch.StartNew();
        while (clock.ElapsedMilliseconds < timeoutMs)
        {
            AutomationElement found = null;
            try
            {
                found = AutomationElement.RootElement.FindFirst(TreeScope.Children, new AndCondition(ProcessCondition(),
                    new PropertyCondition(AutomationElement.AutomationIdProperty, "QApplication.observatoryMainWindow")));
            }
            catch (ElementNotAvailableException) { found = null; }
            if (found != null) return found;
            CheckAlive();
            Thread.Sleep(200);
        }
        throw new TimeoutException("the main window did not appear");
    }

    /// The menu item of a name under `root`. Below the menu bar, `belowBar` looks only at the items of an open menu,
    /// never at the bar's own: the bar may have an item of the same name - View is both a menu of the bar and a
    /// submenu of Image. Nor is it one of the items in `passed`, the path's items already opened: a submenu may hold
    /// an item of its own name - Pixel Math in Process > Pixel Math.
    private AutomationElement MenuItem(AutomationElement root, string name, int timeoutMs, bool belowBar = false,
                                       IList<AutomationElement> passed = null)
    {
        Stopwatch clock = Stopwatch.StartNew();
        while (clock.ElapsedMilliseconds < timeoutMs)
        {
            AutomationElement item = null;
            try
            {
                Condition condition = new AndCondition(ProcessCondition(),
                    new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.MenuItem),
                    new PropertyCondition(AutomationElement.NameProperty, name));
                if (!belowBar) item = root.FindFirst(TreeScope.Descendants, condition);
                else
                {
                    foreach (AutomationElement candidate in root.FindAll(TreeScope.Descendants, condition))
                    {
                        if (OnMenuBar(candidate)) continue;
                        bool opened = false;
                        if (passed != null)
                        {
                            foreach (AutomationElement earlier in passed)
                            {
                                if (Automation.Compare(earlier, candidate)) { opened = true; break; }
                            }
                        }
                        if (!opened) { item = candidate; break; }
                    }
                }
            }
            catch (ElementNotAvailableException) { item = null; }
            if (item != null) return item;
            CheckAlive();
            Thread.Sleep(200);
        }
        throw new TimeoutException("the menu has no item '" + name + "'");
    }

    /// Whether a menu item is one of the menu bar's own.
    private static bool OnMenuBar(AutomationElement item)
    {
        AutomationElement parent = TreeWalker.ControlViewWalker.GetParent(item);
        return parent != null && parent.Current.ControlType == ControlType.MenuBar;
    }

    /// Opens the menus of a path and returns its items, the last one found and not yet chosen.
    public List<AutomationElement> OpenMenuPath(AutomationElement main, string[] path)
    {
        List<AutomationElement> items = new List<AutomationElement>();
        for (int index = 0; index < path.Length; ++index)
        {
            AutomationElement root = index == 0 ? main : AutomationElement.RootElement;
            AutomationElement item = MenuItem(root, path[index], 10000, index > 0, items);
            items.Add(item);
            if (index < path.Length - 1)
            {
                ((ExpandCollapsePattern)item.GetCurrentPattern(ExpandCollapsePattern.Pattern)).Expand();
                Thread.Sleep(300);
            }
        }
        return items;
    }

    public void CloseMenus(List<AutomationElement> items)
    {
        if (items.Count < 2) return;
        try
        {
            ((ExpandCollapsePattern)items[0].GetCurrentPattern(ExpandCollapsePattern.Pattern)).Collapse();
        }
        catch (Exception) { }
        Thread.Sleep(200);
    }

    /// The combo box of a name in a window, once it is there; null when it does not come.
    public AutomationElement ComboBox(AutomationElement scope, string name, int timeoutMs)
    {
        Stopwatch clock = Stopwatch.StartNew();
        while (clock.ElapsedMilliseconds < timeoutMs)
        {
            AutomationElement combo = null;
            try
            {
                // Of several views' boxes, the last one shown: the views grouped as tabs hide all but the active one's,
                // and a view a run has just opened comes last.
                AutomationElement shown = null;
                foreach (AutomationElement candidate in scope.FindAll(TreeScope.Descendants, new AndCondition(
                    new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.ComboBox),
                    new PropertyCondition(AutomationElement.NameProperty, name))))
                {
                    if (combo == null) combo = candidate;
                    if (!candidate.Current.IsOffscreen) shown = candidate;
                }
                if (shown != null) combo = shown;
            }
            catch (ElementNotAvailableException) { combo = null; }
            if (combo != null) return combo;
            CheckAlive();
            Thread.Sleep(500);
        }
        return null;
    }

    public string ValueOf(AutomationElement element)
    {
        object pattern;
        if (element.TryGetCurrentPattern(ValuePattern.Pattern, out pattern)) return ((ValuePattern)pattern).Current.Value;
        return element.Current.Name;
    }

    /// Chooses an item of a combo box as a person does with the keyboard: a click opens its list, Home and Down
    /// reach an item, Return takes it. The list's own items are not in the accessibility tree, so each item is
    /// tried in turn until the box shows the one wanted.
    public bool ChooseInComboBox(IntPtr window, AutomationElement combo, string wanted)
    {
        for (int item = 0; item < 12; ++item)
        {
            if (ValueOf(combo) == wanted) return true;
            System.Windows.Rect place = combo.Current.BoundingRectangle;
            PostClick(window, (int)(place.X + place.Width / 2), (int)(place.Y + place.Height / 2));
            Thread.Sleep(700);
            PostKey(window, 0x24);
            for (int down = 0; down < item; ++down)
            {
                Thread.Sleep(80);
                PostKey(window, 0x28);
            }
            Thread.Sleep(150);
            PostKey(window, 0x0D);
            Thread.Sleep(900);
        }
        if (ValueOf(combo) == wanted) return true;
        // A long list - SPCC's sensors - is searched by typing the item's text into the open list.
        System.Windows.Rect box = combo.Current.BoundingRectangle;
        PostClick(window, (int)(box.X + box.Width / 2), (int)(box.Y + box.Height / 2));
        Thread.Sleep(700);
        foreach (char typed in wanted) AnCaptureNative.PostMessage(window, 0x0102, (IntPtr)typed, IntPtr.Zero);
        Thread.Sleep(300);
        PostKey(window, 0x0D);
        Thread.Sleep(900);
        return ValueOf(combo) == wanted;
    }

    public bool IsEnabled(AutomationElement element)
    {
        try { return element.Current.IsEnabled; } catch (ElementNotAvailableException) { return false; }
    }

    // ---- The controls of a process window, read and set through UI Automation.

    /// The object name of a control: the last part of the identifier the toolkit gives it, which is the path of
    /// object names from the application down to it.
    public static string ObjectName(AutomationElement element)
    {
        string id = element.Current.AutomationId ?? "";
        int dot = id.LastIndexOf('.');
        return dot < 0 ? id : id.Substring(dot + 1);
    }

    private static string TypeName(AutomationElement element)
    {
        return element.Current.ControlType.ProgrammaticName.Replace("ControlType.", "");
    }

    /// Every control of a window: `type  object name  name`, one a line. A process window is small enough to be
    /// walked whole, which the main window with a large image open is not.
    public List<string> DescribeControls(IntPtr window)
    {
        List<string> lines = new List<string>();
        AutomationElement root = Root(window);
        foreach (AutomationElement element in root.FindAll(TreeScope.Descendants, Condition.TrueCondition))
        {
            try
            {
                if (element.Current.ControlType == ControlType.Text || element.Current.ControlType == ControlType.Group
                    || element.Current.ControlType == ControlType.Pane) continue;
                lines.Add(string.Format("{0,-12} {1,-36} {2}", TypeName(element), ObjectName(element), element.Current.Name));
            }
            catch (ElementNotAvailableException) { }
        }
        return lines;
    }

    /// The control of a window that `name` names: by its object name, as the guide's inventory records it, or else
    /// by the name it shows. Null when the window has none.
    public AutomationElement Control(IntPtr window, string name)
    {
        // A window with tabs holds a control of one name on each - Image Integration's Add Folder on every source
        // tab - so the one shown wins; of the controls named by what they show, a tab wins.
        AutomationElement root = Root(window);
        AutomationElement hiddenObject = null, tab = null, shown = null, hiddenShown = null;
        foreach (AutomationElement element in root.FindAll(TreeScope.Descendants, Condition.TrueCondition))
        {
            try
            {
                bool offscreen = element.Current.IsOffscreen;
                if (ObjectName(element) == name)
                {
                    if (!offscreen) return element;
                    if (hiddenObject == null) hiddenObject = element;
                    continue;
                }
                if (element.Current.Name != name || element.Current.ControlType == ControlType.Text) continue;
                if (element.Current.ControlType == ControlType.TabItem && !offscreen) { if (tab == null) tab = element; }
                else if (!offscreen) { if (shown == null) shown = element; }
                else if (hiddenShown == null) hiddenShown = element;
            }
            catch (ElementNotAvailableException) { }
        }
        return hiddenObject ?? tab ?? shown ?? hiddenShown;
    }

    /// Presses a control on a thread of its own, as Choose does a menu item: a button may open a modal dialog.
    public Task Press(IntPtr window, string name)
    {
        // A control a tab just shown builds - Image Integration's Workflow sub-tabs - is given time to appear.
        AutomationElement control = Control(window, name);
        Stopwatch appearing = Stopwatch.StartNew();
        while (control == null && appearing.ElapsedMilliseconds < 10000)
        {
            CheckAlive();
            Thread.Sleep(500);
            control = Control(window, name);
        }
        if (control == null) throw new InvalidOperationException("the window has no control '" + name + "'");
        if (!IsEnabled(control)) throw new InvalidOperationException("'" + name + "' is disabled");
        bool isTab = control.Current.ControlType == ControlType.TabItem;
        Task pressing = Task.Run(delegate
        {
            object pattern;
            try
            {
                if (control.TryGetCurrentPattern(InvokePattern.Pattern, out pattern)) ((InvokePattern)pattern).Invoke();
                else if (control.TryGetCurrentPattern(TogglePattern.Pattern, out pattern)) ((TogglePattern)pattern).Toggle();
                else if (control.TryGetCurrentPattern(SelectionItemPattern.Pattern, out pattern)) ((SelectionItemPattern)pattern).Select();
                // A tab is pressed when its page is shown: the window may still be busy with what came before -
                // Image Integration fills its frame list after an import - and the next control looked up would be
                // the one of the page that was shown.
                if (isTab && control.TryGetCurrentPattern(SelectionItemPattern.Pattern, out pattern))
                {
                    Stopwatch selecting = Stopwatch.StartNew();
                    while (!((SelectionItemPattern)pattern).Current.IsSelected && selecting.ElapsedMilliseconds < 15000)
                        Thread.Sleep(100);
                }
            }
            catch (Exception) { }
        });
        // A tab opens no dialog, so its press is waited for; a button's may open a modal one and is not.
        if (isTab) pressing.Wait(20000);
        return pressing;
    }

    /// Puts every scroll bar of the main window at the middle of its range, so a view zoomed past its window shows
    /// the middle of its image. Returns how many it moved.
    public int ScrollToMiddle(AutomationElement main)
    {
        int moved = 0;
        AutomationElementCollection bars = main.FindAll(TreeScope.Descendants,
            new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.ScrollBar));
        foreach (AutomationElement bar in bars)
        {
            object pattern;
            if (!bar.TryGetCurrentPattern(RangeValuePattern.Pattern, out pattern)) continue;
            RangeValuePattern range = (RangeValuePattern)pattern;
            if (range.Current.Maximum <= range.Current.Minimum || range.Current.IsReadOnly) continue;
            range.SetValue(Math.Round((range.Current.Minimum + range.Current.Maximum) / 2.0));
            ++moved;
        }
        return moved;
    }

    /// Presses a button of the main window by the name it shows, once it is there and enabled: a button of the active
    /// image view's toolbar. It is looked for by its type and its name only, because walking the whole main window
    /// with a large image open times out.
    public Task PressMainButton(AutomationElement main, string name, int timeoutMs)
    {
        Stopwatch clock = Stopwatch.StartNew();
        while (true)
        {
            AutomationElement button = null;
            try
            {
                // A button of the main window, the tab of a view, which makes that view the active one, or an item of the
                // rail - a place, a task. A name ending in "..." is the beginning of the name: a view's tab adds " *"
                // while it is unsaved.
                Condition kind = new OrCondition(new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.Button),
                                                 new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.TabItem),
                                                 new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.ListItem),
                                                 new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.CheckBox));
                // A view's tab wins over a button of the same name: "RGB" is a tab and the channel button of a view.
                bool prefix = name.EndsWith("...");
                string start = prefix ? name.Substring(0, name.Length - 3) : name;
                foreach (AutomationElement candidate in main.FindAll(TreeScope.Descendants, kind))
                {
                    string shown = candidate.Current.Name;
                    if (!(prefix ? shown.StartsWith(start) : shown == name)) continue;
                    if (candidate.Current.ControlType == ControlType.TabItem && !candidate.Current.IsOffscreen) { button = candidate; break; }
                    if (button == null) button = candidate;
                }
            }
            catch (ElementNotAvailableException) { button = null; }
            if (button != null && IsEnabled(button))
            {
                // An item of the rail is clicked, as a person chooses a place; so is a checkable button - Show
                // Astrometry, a chip of the readout - whose toggle through UI Automation would not reach the view.
                if (button.Current.ControlType == ControlType.ListItem || button.Current.ControlType == ControlType.CheckBox)
                {
                    System.Windows.Rect item = button.Current.BoundingRectangle;
                    if (!item.IsEmpty) PostClick((IntPtr)main.Current.NativeWindowHandle, (int)(item.X + item.Width / 2), (int)(item.Y + item.Height / 2));
                    return Task.FromResult(0);
                }
                // A view's tab is also clicked: selecting it does not make its view the active one when the views sit
                // side by side, each in an area of its own.
                if (button.Current.ControlType == ControlType.TabItem)
                {
                    System.Windows.Rect place = button.Current.BoundingRectangle;
                    IntPtr mainHandle = (IntPtr)main.Current.NativeWindowHandle;
                    object selection;
                    try { if (button.TryGetCurrentPattern(SelectionItemPattern.Pattern, out selection)) ((SelectionItemPattern)selection).Select(); }
                    catch (Exception) { }
                    Thread.Sleep(300);
                    if (!place.IsEmpty) PostClick(mainHandle, (int)(place.X + place.Width / 2), (int)(place.Y + place.Height / 2));
                    return Task.FromResult(0);
                }
                return Task.Run(delegate
                {
                    object pattern;
                    try
                    {
                        if (button.TryGetCurrentPattern(SelectionItemPattern.Pattern, out pattern)) ((SelectionItemPattern)pattern).Select();
                        else if (button.TryGetCurrentPattern(InvokePattern.Pattern, out pattern)) ((InvokePattern)pattern).Invoke();
                    }
                    catch (Exception) { }
                });
            }
            if (clock.ElapsedMilliseconds > timeoutMs) throw new TimeoutException("the main window has no enabled button '" + name + "'");
            CheckAlive();
            Thread.Sleep(500);
        }
    }

    /// Gives a control a value the way its kind takes one: a combo box one of its choices, a check box or a
    /// checkable button `On` or `Off`, a spin box or a slider a number, a text field a text. Returns what was done.
    public string SetControl(IntPtr window, string name, string value)
    {
        AutomationElement control = Control(window, name);
        if (control == null) throw new InvalidOperationException("the window has no control '" + name + "'");
        if (!IsEnabled(control)) throw new InvalidOperationException("'" + name + "' is disabled");
        object pattern;
        if (control.Current.ControlType == ControlType.ComboBox)
        {
            // A list the window fills on a worker - SPCC's sensors after a readiness check - is given time to fill.
            Stopwatch filling = Stopwatch.StartNew();
            bool chosen = ChooseInComboBox(window, control, value);
            while (!chosen && filling.ElapsedMilliseconds < 30000)
            {
                CheckAlive();
                Thread.Sleep(1000);
                control = Control(window, name) ?? control;
                chosen = ChooseInComboBox(window, control, value);
            }
            if (!chosen) throw new InvalidOperationException("'" + name + "' has no choice '" + value + "'");
            return name + " shows " + value;
        }
        if (value == "On" || value == "Off")
        {
            if (!control.TryGetCurrentPattern(TogglePattern.Pattern, out pattern)) throw new InvalidOperationException("'" + name + "' is not switched on and off");
            ToggleState wanted = value == "On" ? ToggleState.On : ToggleState.Off;
            TogglePattern toggle = (TogglePattern)pattern;
            for (int attempt = 0; attempt < 2 && toggle.Current.ToggleState != wanted; ++attempt)
            {
                toggle.Toggle();
                Thread.Sleep(300);
            }
            if (toggle.Current.ToggleState != wanted) throw new InvalidOperationException("'" + name + "' did not go " + value);
            return name + " is " + value;
        }
        if (control.TryGetCurrentPattern(RangeValuePattern.Pattern, out pattern))
        {
            double number;
            if (!double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out number)) throw new InvalidOperationException("'" + name + "' takes a number, not '" + value + "'");
            RangeValuePattern range = (RangeValuePattern)pattern;
            if (number < range.Current.Minimum || number > range.Current.Maximum) throw new InvalidOperationException(string.Format(CultureInfo.InvariantCulture, "'{0}' takes {1} to {2}, not {3}", name, range.Current.Minimum, range.Current.Maximum, value));
            range.SetValue(number);
            Thread.Sleep(200);
            return string.Format(CultureInfo.InvariantCulture, "{0} is {1}", name, range.Current.Value);
        }
        if (control.TryGetCurrentPattern(ValuePattern.Pattern, out pattern))
        {
            ((ValuePattern)pattern).SetValue(value);
            Thread.Sleep(200);
            return name + " reads " + ((ValuePattern)pattern).Current.Value;
        }
        throw new InvalidOperationException("'" + name + "' takes no value; name it alone to press it");
    }

    /// Waits for a run that a press of Apply started: until `name` - a button the window disables while it runs -
    /// is enabled again, having first been seen disabled or else after `startMs`, a run that short having ended.
    public void WaitForRun(IntPtr window, string name, int startMs, int timeoutMs)
    {
        Stopwatch clock = Stopwatch.StartNew();
        bool started = false;
        while (clock.ElapsedMilliseconds < timeoutMs)
        {
            CheckAlive();
            // A window that is busy, or behind a dialog that is still closing, may not answer: it is running.
            bool enabled = false;
            try
            {
                AutomationElement control = Control(window, name);
                enabled = control != null && IsEnabled(control);
            }
            catch (Exception) { enabled = false; }
            if (!enabled) started = true;
            else if (started || clock.ElapsedMilliseconds >= startMs) return;
            Thread.Sleep(150);
        }
        throw new TimeoutException("the run did not end");
    }

    /// As WaitForRun, for a window that may close when its run ends: a window that is gone has ended its run.
    public void WaitForRunOrClose(IntPtr window, string name, int startMs, int timeoutMs)
    {
        WaitForRunOrClose(window, name, startMs, timeoutMs, null);
    }

    /// The same, answering a message the run shows when it ends - "Calibration completed successfully" - which holds
    /// the window disabled until it is answered: a window that was not among `known`, has no minimize button, and
    /// has an OK button and no Cancel, which a progress window has.
    public void WaitForRunOrClose(IntPtr window, string name, int startMs, int timeoutMs, ICollection<IntPtr> known)
    {
        Stopwatch clock = Stopwatch.StartNew();
        bool started = false;
        while (clock.ElapsedMilliseconds < timeoutMs)
        {
            CheckAlive();
            if (!IsShown(window)) return;
            if (known != null)
            {
                AnShownWindow message = NewWindow(known);
                if (message != null && !HasMinimizeButton(message.Handle))
                {
                    Thread.Sleep(500);
                    known.Add(message.Handle);
                    bool answerable = false;
                    try { answerable = Control(message.Handle, "OK") != null && Control(message.Handle, "Cancel") == null; }
                    catch (Exception) { answerable = false; }
                    if (answerable)
                    {
                        Note("answered '" + message.Title + "' with OK");
                        try { Press(message.Handle, "OK"); } catch (Exception) { }
                        Thread.Sleep(700);
                        continue;
                    }
                }
            }
            bool enabled = false;
            try
            {
                AutomationElement control = Control(window, name);
                enabled = control != null && IsEnabled(control);
            }
            catch (Exception) { enabled = false; }
            if (!enabled) started = true;
            else if (started || clock.ElapsedMilliseconds >= startMs) return;
            Thread.Sleep(150);
        }
        throw new TimeoutException("the run did not end");
    }

    public bool SamePicture(Bitmap one, Bitmap other)
    {
        return Same(one, other);
    }

    /// Clicks in a control, at a place given in percent of its width and its height from its top left corner: a
    /// control drawn by the application - a curve, a plot - takes a point where a person clicks it.
    public string ClickIn(IntPtr window, string name, double xPercent, double yPercent)
    {
        AutomationElement control = Control(window, name);
        if (control == null) throw new InvalidOperationException("the window has no control '" + name + "'");
        if (xPercent < 0 || xPercent > 100 || yPercent < 0 || yPercent > 100) throw new InvalidOperationException("a click is placed in percent of the control, 0 to 100");
        System.Windows.Rect place = control.Current.BoundingRectangle;
        if (place.IsEmpty || place.Width < 1 || place.Height < 1) throw new InvalidOperationException("'" + name + "' is not on the screen");
        int x = (int)Math.Round(place.X + (place.Width - 1) * xPercent / 100.0);
        int y = (int)Math.Round(place.Y + (place.Height - 1) * yPercent / 100.0);
        PostClick(window, x, y);
        Thread.Sleep(300);
        return string.Format(CultureInfo.InvariantCulture, "clicked {0} at {1},{2} of its {3} x {4}", name, x - (int)place.X, y - (int)place.Y, (int)place.Width, (int)place.Height);
    }

    /// Drags inside a control, from one place to another, both in percent of its width and its height.
    public string DragIn(IntPtr window, string name, double fromX, double fromY, double toX, double toY)
    {
        AutomationElement control = Control(window, name);
        if (control == null) throw new InvalidOperationException("the window has no control '" + name + "'");
        foreach (double percent in new double[] { fromX, fromY, toX, toY })
        {
            if (percent < 0 || percent > 100) throw new InvalidOperationException("a drag is placed in percent of the control, 0 to 100");
        }
        System.Windows.Rect place = control.Current.BoundingRectangle;
        if (place.IsEmpty || place.Width < 1 || place.Height < 1) throw new InvalidOperationException("'" + name + "' is not on the screen");
        int x1 = (int)Math.Round(place.X + (place.Width - 1) * fromX / 100.0);
        int y1 = (int)Math.Round(place.Y + (place.Height - 1) * fromY / 100.0);
        int x2 = (int)Math.Round(place.X + (place.Width - 1) * toX / 100.0);
        int y2 = (int)Math.Round(place.Y + (place.Height - 1) * toY / 100.0);
        PostDrag(window, x1, y1, x2, y2);
        Thread.Sleep(400);
        return string.Format(CultureInfo.InvariantCulture, "dragged in {0} from {1},{2} to {3},{4} of its {5} x {6}", name, x1 - (int)place.X, y1 - (int)place.Y, x2 - (int)place.X, y2 - (int)place.Y, (int)place.Width, (int)place.Height);
    }

    /// The control of the application that has the keyboard, or null when none of its controls has it.
    public AutomationElement Focused()
    {
        try
        {
            AutomationElement focused = AutomationElement.FocusedElement;
            return focused != null && focused.Current.ProcessId == (int)m_pid ? focused : null;
        }
        catch (Exception) { return null; }
    }

    /// A button of the window that can hold the keyboard without showing it: its Reset button, or else its Close
    /// button - the window's own, whose identifier is a path, not the title bar's. Null when it has neither.
    public AutomationElement RestingButton(IntPtr window)
    {
        AutomationElement root = Root(window);
        AutomationElement close = null;
        foreach (AutomationElement element in root.FindAll(TreeScope.Descendants,
            new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.Button)))
        {
            try
            {
                if (!(element.Current.AutomationId ?? "").Contains(".") || !element.Current.IsEnabled) continue;
                if (element.Current.Name == "Reset") return element;
                if (close == null && element.Current.Name == "Close") close = element;
            }
            catch (ElementNotAvailableException) { }
        }
        return close;
    }

    /// Gives the keyboard back to a control. A control that was given a value keeps the keyboard, and draws a
    /// focus frame and a blinking caret that the window did not open with and that never lets its picture rest.
    public void Focus(AutomationElement element)
    {
        if (element == null) return;
        try { element.SetFocus(); } catch (Exception) { }
    }

    /// Chooses a menu item on a thread of its own: an item whose action opens a modal dialog does not return until
    /// the dialog closes.
    public Task Choose(AutomationElement item)
    {
        return Task.Run(delegate
        {
            try { ((InvokePattern)item.GetCurrentPattern(InvokePattern.Pattern)).Invoke(); }
            catch (Exception) { }
        });
    }
}
'@

Add-Type -TypeDefinition $source -ReferencedAssemblies UIAutomationClient, UIAutomationTypes, WindowsBase, System.Drawing -Language CSharp

$menuSeparator = ' > '

# The menu path a process page states in its front matter: the path itself, or the first of a list.
function Get-PageMenu([string]$path) {
    $lines = Get-Content -LiteralPath $path -Encoding UTF8
    if ($lines.Count -lt 2 -or $lines[0].Trim() -ne '---') { throw "$path has no front matter" }
    for ($index = 1; $index -lt $lines.Count -and $lines[$index].Trim() -ne '---'; ++$index) {
        if ($lines[$index] -notmatch '^menu:\s*(.*)$') { continue }
        $value = $Matches[1].Trim()
        if (-not $value -and ($index + 1) -lt $lines.Count -and $lines[$index + 1] -match '^\s+-\s+(.*)$') {
            $value = $Matches[1].Trim()
        }
        $value = $value.Trim('"').Trim("'")
        if ($value) { return $value }
    }
    throw "$path states no ``menu``"
}

# Called with -File, a list arrives as one text: pages and images are told apart by commas, captures - a menu's
# label may hold a comma - by semicolons.
$Page = @($Page | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$Open = @($Open | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$Capture = @($Capture | ForEach-Object { $_ -split ';' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
# What to do in a task or a dialog, in order: (control, reply) to press it and answer the message it asks with the
# button of that name, (control, value) to set it, (control, x, y) to click in it, (control, answer) to press it and
# answer the file dialog it opens, (control, wait) to press it and wait for the run it starts, and (control) to press
# it. A value may hold an equals sign; a control's name holds none of `=`, `?`, `@`, `!` and `^`.
function Read-Changes([string[]]$given, [string]$option) {
    $read = New-Object System.Collections.Generic.List[object]
    foreach ($one in @($given | ForEach-Object { $_ -split ';' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })) {
        $at = $one.IndexOf('=')
        if ($at -eq 0) { throw "$option takes ""<control>=<value>"", ""<control>@<x>,<y>"", ""<control>?<path>"", ""<control>!"" or ""<control>"", not ""$one""" }
        if ($one -match '^%wait=(.+)$') {
            $read.Add(@{ control = '%wait'; value = $null; awaited = $Matches[1].Trim() })
        }
        elseif ($one -match '^%until=(.+)$') {
            $read.Add(@{ control = $Matches[1].Trim(); value = $null; until = $true })
        }
        elseif ($one -match '^%keys=(.+)$') {
            $read.Add(@{ control = '%keys'; value = $null; keys = @($Matches[1] -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ }) })
        }
        elseif ($one -match '^%type=(.*)$') {
            $read.Add(@{ control = '%type'; value = $null; typed = $Matches[1] })
        }
        elseif ($one -match '^%shot=([^@]+)(?:@(.+))?$') {
            $read.Add(@{ control = '%shot'; value = $null; shot = $Matches[1].Trim(); part = "$($Matches[2])".Trim() })
        }
        elseif ($one -match '^([^=?@!^]+)\^(.+)$') {
            $read.Add(@{ control = $Matches[1].Trim(); value = $null; reply = $Matches[2].Trim() })
        }
        elseif ($one -match '^([^=?@!]+)\?(.+)$') {
            $read.Add(@{ control = $Matches[1].Trim(); value = $null; answer = $Matches[2].Trim() })
        }
        elseif ($one -match '^([^=?@!]+)!$') {
            $read.Add(@{ control = $Matches[1].Trim(); value = $null; wait = $true })
        }
        elseif ($at -gt 0) {
            $read.Add(@{ control = $one.Substring(0, $at).Trim(); value = $one.Substring($at + 1).Trim() })
        }
        elseif ($one -match '^(.+)@\s*([0-9.]+)\s*,\s*([0-9.]+)\s*>\s*([0-9.]+)\s*,\s*([0-9.]+)$') {
            $invariant = [System.Globalization.CultureInfo]::InvariantCulture
            $read.Add(@{ control = $Matches[1].Trim(); value = $null
                         x = [double]::Parse($Matches[2], $invariant); y = [double]::Parse($Matches[3], $invariant)
                         toX = [double]::Parse($Matches[4], $invariant); toY = [double]::Parse($Matches[5], $invariant) })
        }
        elseif ($one -match '^(.+)@\s*([0-9.]+)\s*,\s*([0-9.]+)$') {
            $read.Add(@{ control = $Matches[1].Trim(); value = $null
                         x = [double]::Parse($Matches[2], [System.Globalization.CultureInfo]::InvariantCulture)
                         y = [double]::Parse($Matches[3], [System.Globalization.CultureInfo]::InvariantCulture) })
        }
        else { $read.Add(@{ control = $one; value = $null }) }
    }
    return ,$read
}
$changes = Read-Changes $Set '-Set'
# What -Prepare runs before the captures: (menu path, changes).
$preparations = New-Object System.Collections.Generic.List[object]
foreach ($given in @($Prepare | ForEach-Object { $_ -split '\|\|' } | Where-Object { $_ -and $_.Trim() })) {
    $at = $given.IndexOf('|')
    $menu = $given
    $rest = ''
    if ($at -ge 0) { $menu = $given.Substring(0, $at); $rest = $given.Substring($at + 1) }
    if (-not $menu.Trim()) { throw "-Prepare takes ""<menu path>|<control>=<value>;..."", not ""$given""" }
    $preparation = @{ name = $null; menu = $menu.Trim(); process = $true; prepare = $true
                      changes = (Read-Changes @($rest) '-Prepare') }
    # `@<button>` with changes opens a dialog of the main window's own - a view's Autostretch Settings - whose changes
    # act at once; without changes it chooses a view's tab or presses the button, as -View does, so the next
    # preparation works on that view.
    if ($preparation.menu.StartsWith('@')) {
        $preparation.button = $preparation.menu.Substring(1).Trim()
        $preparation.process = $false
        if ($preparation.changes.Count -eq 0) { $preparation.prepare = $false; $preparation.view = $true }
    }
    $preparations.Add($preparation)
}
# What -View and -AfterView choose: menu paths, each acting at once; buttons; and the forms of `%`.
function Read-ViewActions([string[]]$given, [string]$option) {
    $actions = New-Object System.Collections.Generic.List[object]
    foreach ($one in @($given | ForEach-Object { $_ -split ';' } | Where-Object { $_ -and $_.Trim() })) {
        $action = @{ name = $null; menu = $one.Trim(); process = $false; prepare = $false; view = $true; changes = @() }
        # `@<button>|<control>=<value>[&...]` opens a dialog by a button of the main window, changes it and closes it.
        if ($action.menu.StartsWith('@') -and $action.menu.Contains('|')) {
            $at = $action.menu.IndexOf('|')
            $action = @{ name = $null; menu = $action.menu.Substring(0, $at).Trim(); process = $false; prepare = $true
                         button = $action.menu.Substring(1, $at - 1).Trim()
                         changes = (Read-Changes @($action.menu.Substring($at + 1).Replace('&', ';')) $option) }
            $actions.Add($action)
            continue
        }
        if ($action.menu.StartsWith('@')) { $action.button = $action.menu.Substring(1).Trim() }
        if ($action.menu -eq '%middle') { $action.middle = $true; $action.button = '%middle' }
        if ($action.menu.StartsWith('%display=')) { $action.display = $action.menu.Substring(9).Trim(); $action.button = '%display' }
        if ($action.menu -match '^%shot=([^@]+)(?:@(.+))?$') {
            $action.shot = $Matches[1].Trim(); $action.part = "$($Matches[2])".Trim(); $action.button = '%shot'
        }
        if ($action.menu -match '^%type=(.*)$') { $action.typed = $Matches[1]; $action.button = '%type' }
        $actions.Add($action)
    }
    return ,$actions
}
$viewActions = Read-ViewActions $View '-View'
$afterActions = Read-ViewActions $AfterView '-AfterView'

$docs = Join-Path $Guide 'docs\en'

# What to capture: (name, the menu path's items).
$captures = New-Object System.Collections.Generic.List[object]
foreach ($given in $Page) {
    $file = $null
    foreach ($candidate in @($given, (Join-Path $docs $given))) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $file = (Resolve-Path -LiteralPath $candidate).Path; break }
    }
    if (-not $file) { throw "$given is not a page of $docs" }
    $captures.Add(@{ name = [System.IO.Path]::GetFileNameWithoutExtension($file); menu = (Get-PageMenu $file); process = $true })
}
foreach ($given in $Capture) {
    $at = $given.IndexOf('=')
    if ($at -lt 1 -or $at -eq $given.Length - 1) { throw "-Capture takes ""<name>=<menu path>"", not ""$given""" }
    $what = $given.Substring($at + 1).Trim()
    if ($what.StartsWith('@')) {
        $captures.Add(@{ name = $given.Substring(0, $at).Trim(); menu = $what; button = $what.Substring(1).Trim(); process = $false })
    }
    else {
        $captures.Add(@{ name = $given.Substring(0, $at).Trim(); menu = $what; process = $false })
    }
}
$shotNames = @($changes | Where-Object { $_.ContainsKey('shot') } | ForEach-Object { $_.shot }) +
             @($viewActions | Where-Object { $_.shot } | ForEach-Object { $_.shot }) +
             @($afterActions | Where-Object { $_.shot } | ForEach-Object { $_.shot })
if ($captures.Count -eq 0 -and -not $MainWindow -and -not $MenuShot -and $shotNames.Count -eq 0) { throw 'nothing to capture: name a -Page, a -Capture, a -MenuShot, a -MainWindow or a %shot' }
$names = @($captures | ForEach-Object { $_.name }) + @($MainWindow | Where-Object { $_ }) + $shotNames
if (@($names | Sort-Object -Unique).Count -ne $names.Count) { throw "two captures have one name: $($names -join ', ')" }
$taskCrop = $Crop
if (-not $taskCrop) { $taskCrop = 'task' }

if ($WhatIf) {
    # What the run would save, a picture a line, and nothing started.
    foreach ($shot in $captures) {
        $what = "a task, cropped to $taskCrop"
        if ($Float) { $what = 'the task floated in a window of its own' }
        if (-not $shot.process) { $what = "what it opens: a task cropped to $taskCrop, or its dialog" }
        Write-Output "$($shot.name).png    $($shot.menu): $what"
        if ($Apply) {
            Write-Output "$($shot.name)-before.png    the image's view before Apply"
            Write-Output "$($shot.name)-after.png    the image's view after Apply"
        }
    }
    foreach ($name in $shotNames) { Write-Output "$name.png    the main window, as -Set, -View or -AfterView says" }
    if ($MenuShot) { Write-Output "$($MenuShot.Split('=')[0].Trim()).png    the main window with its menus open" }
    if ($MainWindow) {
        $part = 'whole'
        if ($MainCrop) { $part = "cropped to $MainCrop" }
        Write-Output "$MainWindow.png    the main window, $part"
    }
    exit 0
}

$images = @($Open | ForEach-Object { (Resolve-Path -LiteralPath $_).Path })
$Application = (Resolve-Path -LiteralPath $Application).Path
$deployment = Split-Path $Application

New-Item -ItemType Directory -Force -Path $Output | Out-Null
$Output = (Resolve-Path -LiteralPath $Output).Path
$ownStorage = -not $Storage
if ($ownStorage) { $Storage = Join-Path ([System.IO.Path]::GetTempPath()) ('afternight-guide-' + [System.Guid]::NewGuid().ToString('N').Substring(0, 8)) }
if ((Test-Path -LiteralPath $Storage) -and -not ($ReuseStorage -and -not $ownStorage)) { throw "$Storage exists; the application starts from a storage root of its own, a new folder" }
New-Item -ItemType Directory -Force -Path $Storage | Out-Null
$Storage = (Resolve-Path -LiteralPath $Storage).Path

$start = New-Object System.Diagnostics.ProcessStartInfo
$start.FileName = $Application
$start.WorkingDirectory = $deployment
$start.UseShellExecute = $false
$systemRoot = $env:SystemRoot
$start.EnvironmentVariables['PATH'] = "$deployment;$systemRoot\System32;$systemRoot"
$start.EnvironmentVariables['AFTERNIGHT_TEST_STORAGE_ROOT'] = $Storage
# The application starts its one shell, the Observatory shell, in the look a new profile has: the dark theme, the
# default preset, the Compact density, the menu bar shown on Alt.
foreach ($name in @('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM', 'QT_SCALE_FACTOR', 'QT_SCREEN_SCALE_FACTORS', 'PYTHONHOME', 'PYTHONPATH', 'AFTERNIGHT_SHELL')) {
    if ($start.EnvironmentVariables.ContainsKey($name)) { $start.EnvironmentVariables.Remove($name) }
}
# A picture is at 100 % whatever the monitor is set to, and writes its numbers as the guide does whatever the
# machine's regional format is: the toolkit takes its locale from LANG.
$start.EnvironmentVariables['QT_ENABLE_HIGHDPI_SCALING'] = '0'
$start.EnvironmentVariables['QT_FONT_DPI'] = '96'
$start.EnvironmentVariables['LANG'] = 'en_US'

$failed = $false
$process = [System.Diagnostics.Process]::Start($start)
$shots = New-Object AnScreenshots $process

# Saves a picture of the main window, whole or cropped to a part named by its object name - or to the task a part of
# a task names: `task`, `column`, `canvas` - and the upper `fraction` of that part.
function Save-MainPicture([string]$name, [string]$part, [double]$fraction = 1.0) {
    $moved = $false
    $pointer = $shots.MovePointerOut($mainHandle, [ref]$moved)
    try {
        $rested = $false
        $picture = $shots.SettledPicture($mainHandle, $SettleSeconds * 1000, [ref]$rested)
    }
    finally {
        if ($moved) { $shots.PutPointerBack($pointer) }
    }
    try {
        # A popup open over the window - the command palette, a menu - is laid over its picture where it stands.
        if ($shots.HasPopup()) {
            $picture.Dispose()
            $picture = $shots.MenuPicture($mainHandle)
        }
        if ($part -and $part -ne 'window') {
            $element = $null
            $task = $shots.TaskPage($main)
            if ($part -eq 'view') {
                # The image of the image view, without the margins beside an image fitted to its view.
                $cropped = $shots.CroppedTo($picture, $mainHandle, $shots.ViewImagePlace($main), $fraction)
                if (-not $cropped) { throw "the main window shows no image view to crop the picture $name to" }
                $picture.Dispose()
                $picture = $cropped
                $part = 'window'
            }
            switch ($part) {
                'window' { $element = $null }
                'task' { $element = $task }
                'column' { if ($task) { $element = $shots.ByObject($task, 'pageColumn', 8) } }
                'canvas' { if ($task) { $element = $shots.ByObject($task, 'pageCanvas', 8) } }
                default { $element = $shots.ByObject($main, $part, 12) }
            }
            if ($part -ne 'window') {
                if (-not $element) { throw "the main window shows no '$part' to crop the picture $name to" }
                $cropped = $shots.Cropped($picture, $mainHandle, $element, $fraction)
                if (-not $cropped) { throw "'$part' has no place on the screen" }
                $picture.Dispose()
                $picture = $cropped
            }
        }
        $file = Join-Path $Output ($name + '.png')
        $picture.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
        $settled = 'at rest'
        if (-not $rested) { $settled = "still changing after $SettleSeconds s" }
        $shots.Note("saved $file, $($picture.Width) x $($picture.Height), $settled")
        return $picture
    }
    catch {
        $picture.Dispose()
        throw
    }
}

# Chooses the items of a menu path from the menu bar, which shows on Alt. Returns the choice of the last one, which is
# done while what it opened is modal: a dialog's run keeps it, and UI Automation answers nothing else of the
# application until it returns.
function Invoke-MenuPath([string]$menuPath) {
    $path = [string[]]($menuPath -split [regex]::Escape($menuSeparator) | ForEach-Object { $_.Trim() })
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    while ($true) {
        $shots.ShowMenuBar($main)
        $items = $shots.OpenMenuPath($main, $path)
        $last = $items[$items.Count - 1]
        if ($shots.IsEnabled($last)) {
            # The menus open now are no window the choice opens.
            $script:openMenus = @($shots.Windows() | ForEach-Object { $_.Handle })
            $choice = $shots.Choose($last)
            # A choice through UI Automation leaves its menus open, as a click would not: they are closed, by keys
            # posted to them, which a modal dialog the choice opened does not hold up.
            Start-Sleep -Milliseconds 400
            $shots.CloseOpenMenus()
            return $choice
        }
        $shots.CloseMenus($items)
        if ($clock.Elapsed.TotalSeconds -gt $OpenTimeoutSeconds) { throw "'$menuPath' stayed disabled; does it need an image the run did not open?" }
        Start-Sleep -Milliseconds 1000
    }
}

# Does what a view action says: a menu item, a button of the main window, the display mode, the scroll bars at their
# middle, a picture of the main window.
function Invoke-ViewAction($action) {
    if ($action.typed) {
        # Typed into what has the keyboard: a popup the last action opened - the command palette - or the window.
        $target = $mainHandle
        $popup = $shots.NewPopup((New-Object 'System.Collections.Generic.HashSet[IntPtr]'))
        if ($popup) { $target = $popup.Handle }
        $shots.PostText($target, $action.typed)
        Start-Sleep -Milliseconds 800
        $shots.Note("typed '$($action.typed)'")
        return
    }
    if ($action.shot) {
        $picture = Save-MainPicture $action.shot $action.part
        $picture.Dispose()
        return
    }
    if ($action.middle) { $shots.Note("$($shots.ScrollToMiddle($main)) scroll bars put at their middle") }
    elseif ($action.display) {
        if (-not $shots.ChooseDisplayMode($main, $action.display)) { throw "the active view has no display mode '$($action.display)'" }
    }
    elseif ($action.window -or ($action.prepare -and $action.button)) {
        # A dialog a button of the main window opens - a view's Autostretch Settings - is changed and closed.
        $known = New-Object 'System.Collections.Generic.HashSet[IntPtr]'
        foreach ($shown in $shots.Windows()) { [void]$known.Add($shown.Handle) }
        [void]$shots.PressMainButton($main, $action.button, $OpenTimeoutSeconds * 1000)
        $opened = $null
        $asked = [System.Diagnostics.Stopwatch]::StartNew()
        while (-not $opened -and $asked.ElapsedMilliseconds -lt 20000) {
            $opened = $shots.NewWindow($known)
            if (-not $opened) { Start-Sleep -Milliseconds 200 }
        }
        if (-not $opened) { throw "'@$($action.button)' opened no window" }
        Start-Sleep -Milliseconds 800
        foreach ($change in $action.changes) { $shots.Note($shots.SetControl($opened.Handle, $change.control, $change.value)); Start-Sleep -Milliseconds 400 }
        $shots.PostKey($opened.Handle, 0x1B)
        Start-Sleep -Milliseconds 700
        if ($shots.IsShown($opened.Handle)) { $shots.Close($opened.Handle); Start-Sleep -Milliseconds 700 }
    }
    elseif ($action.button) { [void]$shots.PressMainButton($main, $action.button, $OpenTimeoutSeconds * 1000) }
    else { [void](Invoke-MenuPath $action.menu).Wait(30000) }
    $rested = $false
    $shots.SettledPicture($mainHandle, $SettleSeconds * 1000, [ref]$rested).Dispose()
    $shots.Note("chose '$($action.menu)'")
}

# Waits for a task's run to end: its Reset, or its Apply where it has no Reset, enabled again once it was seen disabled
# or after three seconds - or the task's page gone, a task that closes itself when its run ends (OK). A message the run
# ends with, OK and no Cancel, is answered.
function Wait-TaskRun([string]$applyButton, [System.Collections.Generic.HashSet[IntPtr]]$known) {
    $watched = 'Reset'
    if (-not $shots.Control($mainHandle, 'Reset')) { $watched = $applyButton }
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    $started = $false
    while ($clock.ElapsedMilliseconds -lt ($RunTimeoutSeconds * 1000)) {
        $shots.CheckAlive()
        if ($shots.TaskPageId($main) -ne $openedTask) { return }
        foreach ($shown in $shots.Windows()) {
            if ($known.Contains($shown.Handle) -or -not $shown.Title -or $shots.HasMinimizeButton($shown.Handle)) { continue }
            if (-not [AnScreenshots]::IsWorkWindowClass($shown.ClassName)) { continue }
            try {
                if ($shots.Control($shown.Handle, 'OK') -and -not $shots.Control($shown.Handle, 'Cancel')) {
                    $shots.Note("answering '$($shown.Title)' with OK")
                    [void]$shots.Press($shown.Handle, 'OK')
                    [void]$known.Add($shown.Handle)
                }
            }
            catch { }
        }
        $enabled = $false
        try {
            $control = $shots.Control($mainHandle, $watched)
            $enabled = $control -and $shots.IsEnabled($control)
        }
        catch { $enabled = $false }
        if (-not $enabled) { $started = $true }
        elseif ($started -or $clock.ElapsedMilliseconds -ge 3000) { return }
        Start-Sleep -Milliseconds 300
    }
    throw "the run did not end within -RunTimeoutSeconds"
}

try {
    $shots.Note("started $Application with the storage root $Storage")
    $main = $shots.MainWindow(120000)
    $mainHandle = [IntPtr]$main.Current.NativeWindowHandle
    Start-Sleep -Milliseconds 1500
    $shots.Place($mainHandle, $Left, $Top, $Width, $Height)
    Start-Sleep -Milliseconds 800
    $shots.Note("main window placed at $Left,$Top, $Width x $Height")

    foreach ($image in $images) {
        $known = New-Object 'System.Collections.Generic.HashSet[IntPtr]'
        foreach ($shown in $shots.Windows()) { [void]$known.Add($shown.Handle) }
        $choice = Invoke-MenuPath ('File' + $menuSeparator + 'Open')
        $shots.AnswerNativeDialog($image, 20000)
        $shots.Note("asked for $image")
        # A raw color-filter frame is opened after a question - debayer it first? - that -OpenAnswer answers.
        if ($OpenAnswer) {
            $asked = [System.Diagnostics.Stopwatch]::StartNew()
            $question = $null
            while (-not $question -and $asked.ElapsedMilliseconds -lt 20000) {
                $question = $shots.NewWindow($known)
                if ($question -and $shots.HasMinimizeButton($question.Handle)) { [void]$known.Add($question.Handle); $question = $null }
                if (-not $question) { Start-Sleep -Milliseconds 200 }
            }
            if ($question) {
                Start-Sleep -Milliseconds 500
                [void]$shots.Press($question.Handle, $OpenAnswer)
                $shots.Note("answered '$($question.Title)' with $OpenAnswer")
            }
        }
        Start-Sleep -Milliseconds 1500
    }

    # The images -Open opened are in their views before anything is done with them: a process chosen while the image
    # still loads is answered with a message.
    if ($images.Count -gt 0) {
        $clock = [System.Diagnostics.Stopwatch]::StartNew()
        while (-not $shots.HasDisplayMode($main)) {
            if ($clock.Elapsed.TotalSeconds -gt $OpenTimeoutSeconds) { throw "the images did not open within -OpenTimeoutSeconds" }
            Start-Sleep -Milliseconds 500
        }
        $rested = $false
        $shots.SettledPicture($mainHandle, $SettleSeconds * 1000, [ref]$rested).Dispose()
    }
    # The images -Open opened are shown as -Display says; an image a capture leaves open is, before -MainWindow.
    if ($Display -and $images.Count -gt 0) {
        $clock = [System.Diagnostics.Stopwatch]::StartNew()
        while (-not $shots.ChooseDisplayMode($main, $Display)) {
            if ($clock.Elapsed.TotalSeconds -gt $OpenTimeoutSeconds) { throw "no image opened to show as $Display" }
            Start-Sleep -Milliseconds 1000
        }
        $rested = $false
        $shots.SettledPicture($mainHandle, $SettleSeconds * 1000, [ref]$rested).Dispose()
        $shots.Note("the image is shown as $Display")
    }

    # The preparations run first, each as a capture that saves nothing and applies what it set.
    $steps = New-Object System.Collections.Generic.List[object]
    foreach ($preparation in $preparations) { $steps.Add($preparation) }
    foreach ($action in $viewActions) { $steps.Add($action) }
    foreach ($shot in $captures) { $shot.prepare = $false; $shot.changes = $changes; $steps.Add($shot) }

    foreach ($shot in $steps) {
        if ($shot.view) { Invoke-ViewAction $shot; continue }
        if ($shot.prepare -and -not $shot.process) { Invoke-ViewAction $shot; continue }

        # The pair's first half: the image's view as the workspace shows it, before the task opens over it.
        $pictureBefore = $null
        if ($Apply -and -not $shot.prepare -and $shot.process) {
            $pictureBefore = Save-MainPicture ($shot.name + '-before') 'view' (2.0 / 3.0)
        }

        # What the step opens: a task, shown as a page of the main window, or a window of its own - a dialog.
        $before = New-Object 'System.Collections.Generic.HashSet[IntPtr]'
        foreach ($window in $shots.Windows()) { [void]$before.Add($window.Handle) }
        $taskBefore = $shots.TaskPageId($main)
        if ($shot.button) { $choice = $shots.PressMainButton($main, $shot.button, $OpenTimeoutSeconds * 1000) }
        else {
            $choice = Invoke-MenuPath $shot.menu
            foreach ($handle in $script:openMenus) { [void]$before.Add($handle) }
        }

        # A modal dialog keeps the choice from returning, and a task is looked for only once it has: until then UI
        # Automation would wait for the dialog with every question.
        $clock = [System.Diagnostics.Stopwatch]::StartNew()
        $window = $null
        $task = $null
        while (-not $window -and -not $task) {
            $shots.CheckAlive()
            if ($clock.Elapsed.TotalSeconds -gt 60) { throw "'$($shot.menu)' opened neither a task nor a window of its own" }
            $window = $shots.NewWindow($before)
            if ($window) { break }
            # A panel a button opens as a popup - a view's Autostretch Settings - is taken as its own window. A menu
            # path's own menus are popups too, so a popup is looked for only after a button.
            if ($shot.button) {
                $window = $shots.NewPopup($before)
                if ($window) { break }
            }
            if (-not $choice.IsCompleted) { Start-Sleep -Milliseconds 200; continue }
            $taskId = $shots.TaskPageId($main)
            if ($taskId -and $taskId -ne $taskBefore) { $task = $shots.TaskPage($main); break }
            Start-Sleep -Milliseconds 200
        }
        # A message in place of a page's process - the process refuses the image - is answered with Return.
        # A preparation may open a dialog on purpose - New Project... - which is no message.
        if ($window -and $shot.process -and -not $shot.prepare -and -not $shots.HasMinimizeButton($window.Handle)) {
            # The message is saved beside the pictures, so the failure can be read: what it says is not in the tree
            # UI Automation would answer while the message waits.
            Start-Sleep -Milliseconds 500
            $said = $shots.Picture($window.Handle)
            $said.Save((Join-Path $Output ($shot.name + '-message.png')), [System.Drawing.Imaging.ImageFormat]::Png)
            $said.Dispose()
            $shots.PostKey($window.Handle, 0x0D)
            throw "'$($shot.menu)' was answered with a message, '$($window.Title)', not with the process's task; its picture is $($shot.name)-message.png"
        }
        # Whether a task opened, and which: the element itself is not asked again once its run may have changed it.
        $isTask = $null -ne $task
        $openedTask = ''
        if ($isTask) {
            $openedTask = $task.Current.AutomationId
            # A task's controls are looked for in its page; the main window is the window it is in.
            $shots.SetScope($mainHandle, $task)
            $window = New-Object AnShownWindow
            $window.Handle = $mainHandle
            $window.Title = $task.Current.Name
            $shots.Note("the task '$($task.Current.Name)' shown")
        }
        else { $shots.Note("'$($window.Title)' shown") }
        Start-Sleep -Milliseconds 1000

        if ($List -and -not $shot.prepare) {
            $rested = $false
            $shots.SettledPicture($window.Handle, $SettleSeconds * 1000, [ref]$rested).Dispose()
            foreach ($line in $shots.DescribeControls($window.Handle)) { Write-Output "    $line" }
        }
        # What -Set asks for, once the task has drawn what it opens with; each change is given time to land.
        if ($shot.changes.Count -gt 0) {
            $rested = $false
            $shots.SettledPicture($window.Handle, $SettleSeconds * 1000, [ref]$rested).Dispose()
            # Where the keyboard is matters only to changes made through UI Automation: keys and text posted to a
            # dialog that UI Automation cannot walk leave the keyboard where they put it.
            $throughAutomation = @($shot.changes | Where-Object { -not $_.ContainsKey('keys') -and -not $_.ContainsKey('typed') -and -not $_.ContainsKey('shot') }).Count -gt 0
            $focused = $null
            if ($throughAutomation) { $focused = $shots.Focused() }
            foreach ($change in $shot.changes) {
                if ($change.ContainsKey('keys')) {
                    # Keys posted to the window, which a dialog too large for UI Automation takes: its search field has
                    # the keyboard, Tab reaches its list, Down moves in it.
                    $codes = @{ Tab = 0x09; Return = 0x0D; Escape = 0x1B; Space = 0x20; PageUp = 0x21; PageDown = 0x22
                                End = 0x23; Home = 0x24; Left = 0x25; Up = 0x26; Right = 0x27; Down = 0x28 }
                    foreach ($key in $change.keys) {
                        if (-not $codes.ContainsKey($key)) { throw "%keys knows $($codes.Keys -join ', '), not '$key'" }
                        $shots.PostKey($window.Handle, $codes[$key])
                        Start-Sleep -Milliseconds 250
                    }
                    $shots.Note("posted $($change.keys -join ', ')")
                }
                elseif ($change.ContainsKey('until')) {
                    # A run that shows its end by a control - Image Integration's Open Report, enabled once its run
                    # ends - is waited for, up to -RunTimeoutSeconds.
                    $asked = [System.Diagnostics.Stopwatch]::StartNew()
                    while ($asked.ElapsedMilliseconds -lt ($RunTimeoutSeconds * 1000)) {
                        $shots.CheckAlive()
                        $ready = $false
                        try {
                            $control = $shots.Control($window.Handle, $change.control)
                            $ready = $control -and -not $control.Current.IsOffscreen -and $shots.IsEnabled($control)
                        }
                        catch { $ready = $false }
                        if ($ready) { break }
                        Start-Sleep -Milliseconds 2000
                    }
                    if ($asked.ElapsedMilliseconds -ge ($RunTimeoutSeconds * 1000)) { throw "$($change.control) was not enabled within -RunTimeoutSeconds" }
                    $shots.Note("$($change.control) enabled after $([int]$asked.Elapsed.TotalSeconds) s")
                }
                elseif ($change.ContainsKey('typed')) {
                    # A text typed into the control that has the keyboard, a character at a time.
                    $shots.PostText($window.Handle, $change.typed)
                    $shots.Note("typed '$($change.typed)'")
                }
                elseif ($change.ContainsKey('shot')) {
                    # The main window as it is now, the task still open.
                    $picture = Save-MainPicture $change.shot $change.part
                    $picture.Dispose()
                }
                elseif ($change.ContainsKey('awaited')) {
                    # A run that ends with a message - Image Integration's "Complete" - is waited for, up to
                    # -RunTimeoutSeconds, and the message answered with OK; a message of failure stops the capture.
                    $asked = [System.Diagnostics.Stopwatch]::StartNew()
                    $ended = $null
                    while (-not $ended -and $asked.ElapsedMilliseconds -lt ($RunTimeoutSeconds * 1000)) {
                        $shots.CheckAlive()
                        foreach ($shown in $shots.Windows()) {
                            if (-not $shown.Title -or $shown.Handle -eq $window.Handle -or $shown.Handle -eq $mainHandle) { continue }
                            if ($shown.Title.StartsWith($change.awaited) -or $shown.Title.StartsWith('Failed') -or $shown.Title.StartsWith('Completed with Errors')) { $ended = $shown; break }
                        }
                        if (-not $ended) { Start-Sleep -Milliseconds 1000 }
                    }
                    if (-not $ended) { throw "no '$($change.awaited)' came within -RunTimeoutSeconds" }
                    Start-Sleep -Milliseconds 800
                    $shots.Note("'$($ended.Title)' shown after $([int]$asked.Elapsed.TotalSeconds) s")
                    if (-not $ended.Title.StartsWith($change.awaited)) { throw "the run ended with '$($ended.Title)'" }
                    if ($shots.Control($ended.Handle, 'OK')) { [void]$shots.Press($ended.Handle, 'OK') } else { $shots.PostKey($ended.Handle, 0x0D) }
                    Start-Sleep -Milliseconds 1000
                }
                elseif ($change.ContainsKey('reply')) {
                    # The button opens a message box that asks; it is answered with the button of that name.
                    $known = New-Object 'System.Collections.Generic.HashSet[IntPtr]'
                    foreach ($shown in $shots.Windows()) { [void]$known.Add($shown.Handle) }
                    [void]$shots.Press($window.Handle, $change.control)
                    # A window that only says it is preparing - Image Integration's execution plan - comes first: the
                    # box is the first new window that holds a button of that name.
                    $box = $null
                    $asked = [System.Diagnostics.Stopwatch]::StartNew()
                    while (-not $box -and $asked.ElapsedMilliseconds -lt ($OpenTimeoutSeconds * 1000)) {
                        $shots.CheckAlive()
                        foreach ($shown in $shots.Windows()) {
                            if ($known.Contains($shown.Handle) -or -not $shown.Title -or -not [AnScreenshots]::IsWorkWindowClass($shown.ClassName)) { continue }
                            try { if ($shots.Control($shown.Handle, $change.reply)) { $box = $shown; break } } catch { }
                        }
                        if (-not $box) { Start-Sleep -Milliseconds 300 }
                    }
                    if (-not $box) { throw "$($change.control) opened no message to answer" }
                    Start-Sleep -Milliseconds 500
                    [void]$shots.Press($box.Handle, $change.reply)
                    $asked.Restart()
                    while ($shots.IsShown($box.Handle) -and $asked.ElapsedMilliseconds -lt 10000) { Start-Sleep -Milliseconds 100 }
                    $shots.Note("pressed $($change.control) and answered '$($box.Title)' with $($change.reply)")
                }
                elseif ($change.ContainsKey('answer')) {
                    # The button opens the system's dialog; what it then loads disables it until it is loaded.
                    [void]$shots.Press($window.Handle, $change.control)
                    $shots.AnswerNativeDialog($change.answer, 20000)
                    $shots.WaitForRun($window.Handle, $change.control, 1500, $OpenTimeoutSeconds * 1000)
                    $shots.Note("pressed $($change.control) and answered $($change.answer)")
                }
                elseif ($change.ContainsKey('wait')) {
                    [void]$shots.Press($window.Handle, $change.control)
                    $shots.Note("pressed $($change.control)")
                    $shots.WaitForRun($window.Handle, $change.control, 5000, $RunTimeoutSeconds * 1000)
                    $shots.Note("the run $($change.control) started has ended")
                }
                elseif ($change.ContainsKey('toX')) {
                    $shots.Note($shots.DragIn($window.Handle, $change.control, $change.x, $change.y, $change.toX, $change.toY))
                }
                elseif ($change.ContainsKey('x')) {
                    $shots.Note($shots.ClickIn($window.Handle, $change.control, $change.x, $change.y))
                }
                elseif ($null -eq $change.value) {
                    [void]$shots.Press($window.Handle, $change.control)
                    $shots.Note("pressed $($change.control)")
                }
                else {
                    $shots.Note($shots.SetControl($window.Handle, $change.control, $change.value))
                }
                Start-Sleep -Milliseconds 400
            }
            # The keyboard goes back where it was, or to the Reset or Close button when no control had it and the
            # window itself did: a button given the keyboard without a key draws as it did.
            if ($throughAutomation) {
                if (-not $focused -or $focused.Current.ControlType -eq [System.Windows.Automation.ControlType]::Window) {
                    $focused = $shots.RestingButton($window.Handle)
                }
                $shots.Focus($focused)
            }
            Start-Sleep -Milliseconds 600
        }

        # A preview that takes long - a G'MIC filter on one thread - is given -HoldSeconds before the picture.
        if ($HoldSeconds -gt 0 -and -not $shot.prepare) {
            $shots.Note("holding $HoldSeconds s for the task to finish its work")
            Start-Sleep -Seconds $HoldSeconds
        }

        # A preparation applies what it set and is no picture: the captures find the image as it leaves it. One whose
        # last change started its run (`OK!`) has applied what it set already; one with neither Apply nor OK - a tool
        # that saves a file - did what its changes asked.
        $floated = $null
        if ($shot.prepare) {
            $rested = $false
            # A dialog its last change closed - Create in New Project... - did what it was opened for.
            $closed = -not $isTask -and -not $shots.IsShown($window.Handle)
            if (-not $closed) { $shots.SettledPicture($window.Handle, $SettleSeconds * 1000, [ref]$rested).Dispose() }
            $ran = $closed -or ($shot.changes.Count -gt 0 -and $shot.changes[$shot.changes.Count - 1].ContainsKey('wait'))
            if (-not $ran) {
                $applyButton = $null
                if ($shots.Control($window.Handle, 'Apply')) { $applyButton = 'Apply' }
                elseif ($shots.Control($window.Handle, 'OK')) { $applyButton = 'OK' }
                if ($applyButton) {
                    $known = New-Object 'System.Collections.Generic.HashSet[IntPtr]'
                    foreach ($shown in $shots.Windows()) { [void]$known.Add($shown.Handle) }
                    [void]$shots.Press($window.Handle, $applyButton)
                    if ($isTask) { Wait-TaskRun $applyButton $known }
                    else { $shots.WaitForRunOrClose($window.Handle, 'Reset', 3000, $OpenTimeoutSeconds * 1000) }
                }
            }
            $shots.SettledPicture($mainHandle, $SettleSeconds * 1000, [ref]$rested).Dispose()
            $shots.Note("applied '$($shot.menu)'")
        }
        elseif ($isTask -and $Float) {
            # The task in a window of its own, as its header's button floats it.
            $known = New-Object 'System.Collections.Generic.HashSet[IntPtr]'
            foreach ($shown in $shots.Windows()) { [void]$known.Add($shown.Handle) }
            [void]$shots.Press($mainHandle, 'taskHeaderFloat')
            $asked = [System.Diagnostics.Stopwatch]::StartNew()
            while (-not $floated -and $asked.ElapsedMilliseconds -lt 20000) {
                $floated = $shots.NewWindow($known)
                if (-not $floated) { Start-Sleep -Milliseconds 200 }
            }
            if (-not $floated) { throw "'$($shot.menu)' did not float in a window of its own" }
            Start-Sleep -Milliseconds 1000
            $moved = $false
            $pointer = $shots.MovePointerOut($floated.Handle, [ref]$moved)
            try {
                $rested = $false
                $picture = $shots.SettledPicture($floated.Handle, $SettleSeconds * 1000, [ref]$rested)
            }
            finally { if ($moved) { $shots.PutPointerBack($pointer) } }
            $file = Join-Path $Output ($shot.name + '.png')
            $picture.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
            $shots.Note("saved $file, $($picture.Width) x $($picture.Height), the task floated")
            $picture.Dispose()
        }
        elseif ($isTask) {
            $picture = Save-MainPicture $shot.name $taskCrop
            $picture.Dispose()
        }
        else {
            $moved = $false
            $pointer = $shots.MovePointerOut($window.Handle, [ref]$moved)
            try {
                $rested = $false
                $picture = $shots.SettledPicture($window.Handle, $SettleSeconds * 1000, [ref]$rested)
            }
            finally { if ($moved) { $shots.PutPointerBack($pointer) } }
            $file = Join-Path $Output ($shot.name + '.png')
            $picture.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
            $settled = 'at rest'
            if (-not $rested) { $settled = "still changing after $SettleSeconds s" }
            $shots.Note("saved $file, $($picture.Width) x $($picture.Height), $settled")
            $picture.Dispose()
        }

        # The pair: Apply - or OK - and the run waited for; its second half is taken once the task is closed.
        if ($Apply -and -not $shot.prepare) {
            if (-not $isTask -or -not $pictureBefore) { throw "-Apply takes its pair from the image's view around a task; '$($shot.menu)' opened a window" }
            $applyButton = 'Apply'
            if (-not $shots.Control($mainHandle, 'Apply')) { $applyButton = 'OK' }
            $known = New-Object 'System.Collections.Generic.HashSet[IntPtr]'
            foreach ($shown in $shots.Windows()) { [void]$known.Add($shown.Handle) }
            [void]$shots.Press($mainHandle, $applyButton)
            Wait-TaskRun $applyButton $known
            $shots.Note("applied '$($shot.menu)'")
        }

        # What the run left on the screen - a message that it is complete - is dismissed first, the last one shown
        # first: Return answers a message, Escape closes a window that is done.
        for ($pass = 0; $pass -lt 4; ++$pass) {
            $leftOpen = @($shots.Windows() | Where-Object { $_.Handle -ne $window.Handle -and $_.Handle -ne $mainHandle -and (-not $floated -or $_.Handle -ne $floated.Handle) -and -not $before.Contains($_.Handle) -and $_.Title -and [AnScreenshots]::IsWorkWindowClass($_.ClassName) })
            if ($leftOpen.Count -eq 0) { break }
            $shots.Note("dismissing '$($leftOpen[0].Title)'")
            if ($shots.HasMinimizeButton($leftOpen[0].Handle)) { $shots.PostKey($leftOpen[0].Handle, 0x1B) } else { $shots.PostKey($leftOpen[0].Handle, 0x0D) }
            Start-Sleep -Milliseconds 700
            if ($shots.IsShown($leftOpen[0].Handle)) { $shots.PostKey($leftOpen[0].Handle, 0x1B); Start-Sleep -Milliseconds 700 }
        }

        if ($isTask) {
            # A task closes by its header's Close, as a person closes it; its page goes.
            # A floated task closes by its own Close, in its window: a window's close message would reach the
            # application, which asks whether to quit.
            if ($floated -and $shots.IsShown($floated.Handle)) {
                if ($shots.Control($floated.Handle, 'taskHeaderClose')) { [void]$shots.Press($floated.Handle, 'taskHeaderClose') }
                else { $shots.PostKey($floated.Handle, 0x1B) }
                $clock.Restart()
                while ($shots.IsShown($floated.Handle) -and $clock.ElapsedMilliseconds -lt 5000) { Start-Sleep -Milliseconds 100 }
            }
            if ($shots.TaskPageId($main) -eq $openedTask -and $shots.Control($mainHandle, 'taskHeaderClose')) {
                [void]$shots.Press($mainHandle, 'taskHeaderClose')
            }
            $shots.ClearScope($mainHandle)
            $clock.Restart()
            while ($shots.TaskPageId($main) -and $clock.ElapsedMilliseconds -lt 5000) { Start-Sleep -Milliseconds 100 }
            if ($shots.TaskPageId($main)) {
                $others = @($shots.Windows() | Where-Object { $_.Handle -ne $mainHandle -and $_.Title } | ForEach-Object { "'$($_.Title)'" })
                $beside = ''
                if ($others.Count -gt 0) { $beside = "; the application also shows $($others -join ', ')" }
                throw "the task of '$($shot.menu)' did not close$beside"
            }
        }
        else {
            # Escape closes a dialog; one that stays is asked to close.
            $shots.PostKey($window.Handle, 0x1B)
            $clock.Restart()
            while ($shots.IsShown($window.Handle) -and $clock.ElapsedMilliseconds -lt 5000) { Start-Sleep -Milliseconds 100 }
            if ($shots.IsShown($window.Handle)) {
                $shots.Close($window.Handle)
                $clock.Restart()
                while ($shots.IsShown($window.Handle) -and $clock.ElapsedMilliseconds -lt 5000) { Start-Sleep -Milliseconds 100 }
            }
            if ($shots.IsShown($window.Handle)) {
                $others = @($shots.Windows() | Where-Object { $_.Handle -ne $window.Handle -and $_.Handle -ne $mainHandle -and $_.Title } | ForEach-Object { "'$($_.Title)'" })
                $beside = ''
                if ($others.Count -gt 0) { $beside = "; the application also shows $($others -join ', ')" }
                throw "'$($window.Title)' did not close$beside"
            }
        }
        # The pair's second half, once the task is closed and the workspace shows the image again.
        if ($pictureBefore) {
            # The image's view as it now is, at the place and zoom of the first half: the same part of both.
            $pictureAfter = Save-MainPicture ($shot.name + '-after') 'view' (2.0 / 3.0)
            $same = $shots.SamePicture($pictureBefore, $pictureAfter)
            $pictureBefore.Dispose()
            $pictureAfter.Dispose()
            if ($same) { throw "Apply changed nothing in the image's view" }
        }
        Start-Sleep -Milliseconds 500
    }

    # What -AfterView says, once the captures are done: the half of a pair a run made, a view chosen for -MainWindow.
    foreach ($action in $afterActions) { Invoke-ViewAction $action }

    # The menus open along a path, the pointer resting on its last item: the main window's picture with each open
    # menu's own picture laid over it, since a window's own picture leaves out the menus it opened.
    if ($MenuShot) {
        $at = $MenuShot.IndexOf('=')
        if ($at -lt 1) { throw "-MenuShot takes ""<name>=<menu path>"", not ""$MenuShot""" }
        $menuName = $MenuShot.Substring(0, $at).Trim()
        $path = [string[]]($MenuShot.Substring($at + 1) -split [regex]::Escape($menuSeparator) | ForEach-Object { $_.Trim() })
        $shots.ShowMenuBar($main)
        $items = $shots.OpenMenuPath($main, $path)
        $shots.PointAt($items[$items.Count - 1])
        Start-Sleep -Milliseconds 1200
        $picture = $shots.MenuPicture($mainHandle)
        $file = Join-Path $Output ($menuName + '.png')
        $picture.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
        $shots.Note("saved $file, $($picture.Width) x $($picture.Height), with the menus open")
        $picture.Dispose()
        $shots.CloseMenus($items)
        Start-Sleep -Milliseconds 500
    }

    if ($MainWindow) {
        # An image the captures left open - the result of a run - is shown as -Display says, as the opened ones were.
        if ($Display) { [void]$shots.ChooseDisplayMode($main, $Display) }
        $picture = Save-MainPicture $MainWindow $MainCrop
        $picture.Dispose()
    }
}
catch {
    $failed = $true
    Write-Output "capture failed: $($_.Exception.Message)"
}
finally {
    # Nothing of the session is kept: the application is stopped as it stands, without its questions about saving.
    if (-not $process.HasExited) {
        try { $process.Kill() } catch { }
        [void]$process.WaitForExit(15000)
    }
    if ($ownStorage) {
        try { [System.IO.Directory]::Delete($Storage, $true) } catch { Write-Output "the storage root $Storage could not be removed" }
    }
}
if ($failed) { exit 1 }
exit 0
