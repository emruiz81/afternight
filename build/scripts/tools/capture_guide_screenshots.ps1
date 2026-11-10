<#
.SYNOPSIS
Captures the application's windows for the user guide's pictures, the same way every time.

.DESCRIPTION
A page of the guide opens with a picture of its window. This script takes that picture from the application itself:
it starts a deployment's application with a storage root of its own, so nobody's settings are in the picture and
none are touched; gives the main window a fixed place and size; opens the images the pictures need through the
application's own File > Open and the system's file dialog; then, for each capture asked for, opens the process from
the menu a person would use, waits until the window has stopped changing - its preview has landed - and saves the
window, frame and title bar included, as a PNG. It closes each process window as a person would, with Escape, and
stops the application when it is done.

The picture is taken with PrintWindow, which reads the window's own content: another window in front of it does not
get into the picture, and the pointer is never in it. The pointer is moved out of the frame while a picture is
taken, because what it hovers over is drawn highlighted, and put back afterwards. The application is run without
display scaling and in the en_US locale, so a picture is at 100 % and writes its numbers with a decimal point, as
the guide does, whatever the machine is set to.

-Display <mode> chooses how the opened image is shown before anything is captured - `Autostretch` for a linear
master, which is nearly black without it - through the display mode box of the image's toolbar, and waits until the
view has redrawn.

What is captured is named in one of two ways:

  -Page <page.md>            a process page of the guide: the menu path is the page's `menu`, the picture is named
                             after the page (`gamma-correction.md` gives `gamma-correction.png`)
  -Capture "<name>=<menu>"   any menu path, such as "curves=Process > Intensity Transformations > Curves
                             Transformation"; the picture is `<name>.png`

-MainWindow <name> adds a picture of the main window as it is when the captures are done.

A menu item that does not open a window of its own - a submenu, an action that acts at once - ends the run with an
error that names it. So does a page's process that the application answers with a message instead of its window -
it refuses the image, or is open on it already: the script answers the message with Return and stops. A -Capture
takes whatever window its menu item opens, a dialog included.

The PNGs are the captures as taken. build/scripts/tools/optimize_guide_image.py turns one into the file a page
shows, inside the guide's limits, and prints its row for the media provenance page. docs/user-guide/SCREENSHOTS.md
has the whole recipe.

Usage (Windows PowerShell 5.1, which carries UI Automation):
    powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\tools\capture_guide_screenshots.ps1
        -Application bin\msvc\release\AfterNight.exe -Output <folder for the PNGs> [-Open <image>[,<image>...]]
        [-Display <mode>] [-Page <page.md>[,...]] [-Capture "<name>=<menu path>[;...]"] [-MainWindow <name>]
        [-Storage <new folder>]
        [-Guide docs\user-guide] [-Left 80] [-Top 60] [-Width 1600] [-Height 1000] [-SettleSeconds 20]
        [-OpenTimeoutSeconds 180]

It exits 0 when every capture was saved, and 1, having stopped the application, when one was not.
#>
param(
    [Parameter(Mandatory = $true)][string]$Application,
    [Parameter(Mandatory = $true)][string]$Output,
    [string[]]$Open = @(),
    [string]$Display = '',
    [string[]]$Page = @(),
    [string[]]$Capture = @(),
    [string]$MainWindow = '',
    [string]$Storage = '',
    [string]$Guide = '',
    [int]$Left = 80,
    [int]$Top = 60,
    [int]$Width = 1600,
    [int]$Height = 1000,
    [int]$SettleSeconds = 20,
    [int]$OpenTimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing

$source = @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
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

    /// A window that was not among `before` and is one a person works in: it has a title, and is neither the
    /// system's dialog nor one of the toolkit's popups - a menu, a tooltip, a combo's list.
    public AnShownWindow NewWindow(ICollection<IntPtr> before)
    {
        foreach (AnShownWindow window in Windows())
        {
            if (before.Contains(window.Handle) || window.Title.Length == 0) continue;
            if (window.ClassName == "#32770" || !window.ClassName.EndsWith("QWindowIcon")) continue;
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
                    new PropertyCondition(AutomationElement.AutomationIdProperty, "QApplication.afterNightMainWindow")));
            }
            catch (ElementNotAvailableException) { found = null; }
            if (found != null) return found;
            CheckAlive();
            Thread.Sleep(200);
        }
        throw new TimeoutException("the main window did not appear");
    }

    private AutomationElement MenuItem(AutomationElement root, string name, int timeoutMs)
    {
        Stopwatch clock = Stopwatch.StartNew();
        while (clock.ElapsedMilliseconds < timeoutMs)
        {
            AutomationElement item = null;
            try
            {
                item = root.FindFirst(TreeScope.Descendants, new AndCondition(ProcessCondition(),
                    new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.MenuItem),
                    new PropertyCondition(AutomationElement.NameProperty, name)));
            }
            catch (ElementNotAvailableException) { item = null; }
            if (item != null) return item;
            CheckAlive();
            Thread.Sleep(200);
        }
        throw new TimeoutException("the menu has no item '" + name + "'");
    }

    /// Opens the menus of a path and returns its items, the last one found and not yet chosen.
    public List<AutomationElement> OpenMenuPath(AutomationElement main, string[] path)
    {
        List<AutomationElement> items = new List<AutomationElement>();
        for (int index = 0; index < path.Length; ++index)
        {
            AutomationElement root = index == 0 ? main : AutomationElement.RootElement;
            AutomationElement item = MenuItem(root, path[index], 10000);
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
                combo = scope.FindFirst(TreeScope.Descendants, new AndCondition(
                    new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.ComboBox),
                    new PropertyCondition(AutomationElement.NameProperty, name)));
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
        return ValueOf(combo) == wanted;
    }

    public bool IsEnabled(AutomationElement element)
    {
        try { return element.Current.IsEnabled; } catch (ElementNotAvailableException) { return false; }
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
# The name the image view gives the box that chooses between Linear and Autostretch.
$displayModeBox = 'Display Mode'

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

$Application = (Resolve-Path -LiteralPath $Application).Path
$deployment = Split-Path $Application
$repository = Split-Path (Split-Path (Split-Path $PSScriptRoot))
if (-not $Guide) { $Guide = Join-Path $repository 'docs\user-guide' }
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
    $captures.Add(@{ name = $given.Substring(0, $at).Trim(); menu = $given.Substring($at + 1).Trim(); process = $false })
}
if ($captures.Count -eq 0 -and -not $MainWindow) { throw 'nothing to capture: name a -Page, a -Capture or a -MainWindow' }
$names = @($captures | ForEach-Object { $_.name }) + @($MainWindow | Where-Object { $_ })
if (@($names | Sort-Object -Unique).Count -ne $names.Count) { throw "two captures have one name: $($names -join ', ')" }
$images = @($Open | ForEach-Object { (Resolve-Path -LiteralPath $_).Path })

New-Item -ItemType Directory -Force -Path $Output | Out-Null
$Output = (Resolve-Path -LiteralPath $Output).Path
$ownStorage = -not $Storage
if ($ownStorage) { $Storage = Join-Path ([System.IO.Path]::GetTempPath()) ('afternight-guide-' + [System.Guid]::NewGuid().ToString('N').Substring(0, 8)) }
if (Test-Path -LiteralPath $Storage) { throw "$Storage exists; the application starts from a storage root of its own, a new folder" }
New-Item -ItemType Directory -Force -Path $Storage | Out-Null
$Storage = (Resolve-Path -LiteralPath $Storage).Path

$start = New-Object System.Diagnostics.ProcessStartInfo
$start.FileName = $Application
$start.WorkingDirectory = $deployment
$start.UseShellExecute = $false
$systemRoot = $env:SystemRoot
$start.EnvironmentVariables['PATH'] = "$deployment;$systemRoot\System32;$systemRoot"
$start.EnvironmentVariables['AFTERNIGHT_TEST_STORAGE_ROOT'] = $Storage
foreach ($name in @('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM', 'QT_SCALE_FACTOR', 'QT_SCREEN_SCALE_FACTORS', 'PYTHONHOME', 'PYTHONPATH')) {
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
try {
    $shots.Note("started $Application with the storage root $Storage")
    $main = $shots.MainWindow(120000)
    $mainHandle = [IntPtr]$main.Current.NativeWindowHandle
    Start-Sleep -Milliseconds 1500
    $shots.Place($mainHandle, $Left, $Top, $Width, $Height)
    Start-Sleep -Milliseconds 800
    $shots.Note("main window placed at $Left,$Top, $Width x $Height")

    foreach ($image in $images) {
        $items = $shots.OpenMenuPath($main, [string[]]@('File', 'Open'))
        [void]$shots.Choose($items[$items.Count - 1])
        $shots.AnswerNativeDialog($image, 20000)
        $shots.Note("asked for $image")
        Start-Sleep -Milliseconds 1500
    }

    if ($Display) {
        $box = $shots.ComboBox($main, $displayModeBox, $OpenTimeoutSeconds * 1000)
        if (-not $box) { throw "no image opened to show as $Display" }
        if (-not $shots.ChooseInComboBox($mainHandle, $box, $Display)) { throw "the image's display mode has no '$Display'" }
        $rested = $false
        $shots.SettledPicture($mainHandle, $SettleSeconds * 1000, [ref]$rested).Dispose()
        $shots.Note("the image is shown as $Display")
    }

    foreach ($shot in $captures) {
        $path = [string[]]($shot.menu -split [regex]::Escape($menuSeparator) | ForEach-Object { $_.Trim() })
        # The item is enabled once the image it works on has opened.
        $clock = [System.Diagnostics.Stopwatch]::StartNew()
        $item = $null
        while (-not $item) {
            $items = $shots.OpenMenuPath($main, $path)
            $last = $items[$items.Count - 1]
            if ($shots.IsEnabled($last)) { $item = $last; break }
            $shots.CloseMenus($items)
            if ($clock.Elapsed.TotalSeconds -gt $OpenTimeoutSeconds) { throw "'$($shot.menu)' stayed disabled; does it need an image the run did not open?" }
            Start-Sleep -Milliseconds 1000
        }
        $before = New-Object 'System.Collections.Generic.HashSet[IntPtr]'
        foreach ($window in $shots.Windows()) { [void]$before.Add($window.Handle) }
        [void]$shots.Choose($item)

        # The window, once it is shown. A message in place of a page's process - the process refuses the image, or
        # is open on it already - is answered with Return, and is no picture of the process.
        $clock.Restart()
        $window = $null
        while (-not $window) {
            $shots.CheckAlive()
            $window = $shots.NewWindow($before)
            if ($window) { break }
            if ($clock.Elapsed.TotalSeconds -gt 60) { throw "'$($shot.menu)' opened no window of its own" }
            Start-Sleep -Milliseconds 200
        }
        if ($shot.process -and -not $shots.HasMinimizeButton($window.Handle)) {
            $shots.PostKey($window.Handle, 0x0D)
            throw "'$($shot.menu)' was answered with a message, '$($window.Title)', not with the process's window"
        }
        $shots.Note("'$($window.Title)' shown")
        Start-Sleep -Milliseconds 1000

        $moved = $false
        $pointer = $shots.MovePointerOut($window.Handle, [ref]$moved)
        try {
            $rested = $false
            $picture = $shots.SettledPicture($window.Handle, $SettleSeconds * 1000, [ref]$rested)
        }
        finally {
            if ($moved) { $shots.PutPointerBack($pointer) }
        }
        $file = Join-Path $Output ($shot.name + '.png')
        $picture.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
        $settled = 'at rest'
        if (-not $rested) { $settled = "still changing after $SettleSeconds s" }
        $shots.Note("saved $file, $($picture.Width) x $($picture.Height), $settled")
        $picture.Dispose()

        # Escape closes a process window; a window that stays is asked to close.
        $shots.PostKey($window.Handle, 0x1B)
        $clock.Restart()
        while ($shots.IsShown($window.Handle) -and $clock.ElapsedMilliseconds -lt 5000) { Start-Sleep -Milliseconds 100 }
        if ($shots.IsShown($window.Handle)) {
            $shots.Close($window.Handle)
            $clock.Restart()
            while ($shots.IsShown($window.Handle) -and $clock.ElapsedMilliseconds -lt 5000) { Start-Sleep -Milliseconds 100 }
        }
        if ($shots.IsShown($window.Handle)) { throw "'$($window.Title)' did not close" }
        Start-Sleep -Milliseconds 500
    }

    if ($MainWindow) {
        $moved = $false
        $pointer = $shots.MovePointerOut($mainHandle, [ref]$moved)
        try {
            $rested = $false
            $picture = $shots.SettledPicture($mainHandle, $SettleSeconds * 1000, [ref]$rested)
        }
        finally {
            if ($moved) { $shots.PutPointerBack($pointer) }
        }
        $file = Join-Path $Output ($MainWindow + '.png')
        $picture.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
        $shots.Note("saved $file, $($picture.Width) x $($picture.Height)")
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
