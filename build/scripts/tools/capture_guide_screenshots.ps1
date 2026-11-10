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
  -Capture "<name>=@<button>" a button of the main window, by the name it shows - a button of the active image
                             view's toolbar, such as "settings=@Autostretch Settings" - for a window no menu opens

-MainWindow <name> adds a picture of the main window as it is when the captures are done.

A window is captured as it opens unless -Set says what to do in it first:

  -Set "<control>=<value>[;<control>[=<value>]...]"
                             sets controls of each window before its picture is taken, in the order given. A control
                             is named by its object name, as the window's inventory under docs/user-guide/data/ui
                             records it (`shadowsValue`), or by the name it shows (`Apply`). A spin box takes a number
                             written with a decimal point, a combo box one of its choices, a check box or a checkable
                             button `On` or `Off`. A control named without a value is pressed. `<control>@<x>,<y>`
                             clicks in a control the application draws itself - a curve, a plot - at a place given
                             in percent of its width and height from its top left corner, and
                             `<control>@<x>,<y>><x>,<y>` drags from the first place to the second.
                             `<control>?<path>` presses a button that opens the system's file or folder dialog,
                             answers the dialog with the path and waits for what is then loaded. `<control>!`
                             presses a button that starts a run and waits, up to -RunTimeoutSeconds, until the
                             button is enabled again.
  -List                      prints the controls of each window it captures - type, object name and name - which is
                             what -Set can name.
  -Prepare "<menu path>|<control>=<value>[;...]"
                             gives the image the state the captures need, before any of them: opens that process,
                             sets its controls as -Set does, applies it and closes it. A page of a process that
                             works on a stretched image is captured on the linear master after the stretch its own
                             worked example names, so the guide's data stays one file.

-View "<menu path>[;<menu path>...]" chooses menu items that act on the active image or the main window without a
window of their own - "Image > View > Zoom 100%", "View > Session Console", or `@<button>` for a button of the
main window such as "@Clear Session" - once the preparations are done and before the first capture, and waits until the view has
redrawn. A process judged by its detail shows its pair at 100 %: the view zooms around its middle, so the pair is the
middle of the image.

-Apply takes a before-and-after pair of the image the process works on: once the window's picture is saved it saves
the main window as `<name>-before.png`, presses the window's Apply, waits for the run to end and for the view to
rest, and saves the main window again as `<name>-after.png`. Both show the image under the display mode it was
opened in, in the same place and at the same zoom; optimize_guide_image.py's --crop cuts the same part out of both.
A run whose Apply changes nothing in the main window ends with an error.

What a window's run leaves on the screen - the message that it is complete, its progress window - is dismissed
before the window is closed, and an image it opened stays for -MainWindow, shown as -Display says.

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
        [-Set "<control>=<value>[;...]"] [-Prepare "<menu path>|<control>=<value>[;...]"]
        [-View "<menu path>[;...]"] [-Apply] [-List]
        [-Storage <new folder>]
        [-Guide docs\user-guide] [-Left 80] [-Top 60] [-Width 1600] [-Height 1000] [-SettleSeconds 20]
        [-OpenTimeoutSeconds 180] [-RunTimeoutSeconds 3600]

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
    [string[]]$Set = @(),
    [string[]]$Prepare = @(),
    [string[]]$View = @(),
    [switch]$Apply,
    [switch]$List,
    [string]$Storage = '',
    [string]$Guide = '',
    [int]$Left = 80,
    [int]$Top = 60,
    [int]$Width = 1600,
    [int]$Height = 1000,
    [int]$SettleSeconds = 20,
    [int]$OpenTimeoutSeconds = 180,
    [int]$RunTimeoutSeconds = 3600
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
        AutomationElement root = AutomationElement.FromHandle(window);
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
        AutomationElement root = AutomationElement.FromHandle(window);
        AutomationElement shown = null;
        foreach (AutomationElement element in root.FindAll(TreeScope.Descendants, Condition.TrueCondition))
        {
            try
            {
                if (ObjectName(element) == name) return element;
                if (shown == null && element.Current.Name == name && element.Current.ControlType != ControlType.Text) shown = element;
            }
            catch (ElementNotAvailableException) { }
        }
        return shown;
    }

    /// Presses a control on a thread of its own, as Choose does a menu item: a button may open a modal dialog.
    public Task Press(IntPtr window, string name)
    {
        AutomationElement control = Control(window, name);
        if (control == null) throw new InvalidOperationException("the window has no control '" + name + "'");
        if (!IsEnabled(control)) throw new InvalidOperationException("'" + name + "' is disabled");
        return Task.Run(delegate
        {
            object pattern;
            try
            {
                if (control.TryGetCurrentPattern(InvokePattern.Pattern, out pattern)) ((InvokePattern)pattern).Invoke();
                else if (control.TryGetCurrentPattern(TogglePattern.Pattern, out pattern)) ((TogglePattern)pattern).Toggle();
            }
            catch (Exception) { }
        });
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
                button = main.FindFirst(TreeScope.Descendants, new AndCondition(
                    new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.Button),
                    new PropertyCondition(AutomationElement.NameProperty, name)));
            }
            catch (ElementNotAvailableException) { button = null; }
            if (button != null && IsEnabled(button))
            {
                return Task.Run(delegate
                {
                    object pattern;
                    try
                    {
                        if (button.TryGetCurrentPattern(InvokePattern.Pattern, out pattern)) ((InvokePattern)pattern).Invoke();
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
            if (!ChooseInComboBox(window, control, value)) throw new InvalidOperationException("'" + name + "' has no choice '" + value + "'");
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
        AutomationElement root = AutomationElement.FromHandle(window);
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
# What to do in a window, in order: (control, value) to set it, (control, x, y) to click in it, (control, answer) to
# press it and answer the file dialog it opens, (control, wait) to press it and wait for the run it starts, and
# (control) to press it. A value may hold an equals sign; a control's name holds none of `=`, `?`, `@` and `!`.
function Read-Changes([string[]]$given, [string]$option) {
    $read = New-Object System.Collections.Generic.List[object]
    foreach ($one in @($given | ForEach-Object { $_ -split ';' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })) {
        $at = $one.IndexOf('=')
        if ($at -eq 0) { throw "$option takes ""<control>=<value>"", ""<control>@<x>,<y>"", ""<control>?<path>"", ""<control>!"" or ""<control>"", not ""$one""" }
        if ($one -match '^([^=?@!]+)\?(.+)$') {
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
foreach ($given in @($Prepare | Where-Object { $_ -and $_.Trim() })) {
    $at = $given.IndexOf('|')
    $menu = $given
    $rest = ''
    if ($at -ge 0) { $menu = $given.Substring(0, $at); $rest = $given.Substring($at + 1) }
    if (-not $menu.Trim()) { throw "-Prepare takes ""<menu path>|<control>=<value>;..."", not ""$given""" }
    $preparations.Add(@{ name = $null; menu = $menu.Trim(); process = $true; prepare = $true
                         changes = (Read-Changes @($rest) '-Prepare') })
}
# What -View chooses on the image view once the preparations are done: menu paths, each acting at once.
$viewActions = New-Object System.Collections.Generic.List[object]
foreach ($given in @($View | ForEach-Object { $_ -split ';' } | Where-Object { $_ -and $_.Trim() })) {
    $action = @{ name = $null; menu = $given.Trim(); process = $false; prepare = $false; view = $true; changes = @() }
    if ($action.menu.StartsWith('@')) { $action.button = $action.menu.Substring(1).Trim() }
    $viewActions.Add($action)
}

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
    $what = $given.Substring($at + 1).Trim()
    if ($what.StartsWith('@')) {
        $captures.Add(@{ name = $given.Substring(0, $at).Trim(); menu = $what; button = $what.Substring(1).Trim(); process = $false })
    }
    else {
        $captures.Add(@{ name = $given.Substring(0, $at).Trim(); menu = $what; process = $false })
    }
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

    # The images -Open opened are shown as -Display says; an image a capture leaves open is, before -MainWindow.
    if ($Display -and $images.Count -gt 0) {
        $box = $shots.ComboBox($main, $displayModeBox, $OpenTimeoutSeconds * 1000)
        if (-not $box) { throw "no image opened to show as $Display" }
        if (-not $shots.ChooseInComboBox($mainHandle, $box, $Display)) { throw "the image's display mode has no '$Display'" }
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
        $path = [string[]]($shot.menu -split [regex]::Escape($menuSeparator) | ForEach-Object { $_.Trim() })
        # The item is enabled once the image it works on has opened.
        $clock = [System.Diagnostics.Stopwatch]::StartNew()
        $item = $null
        while (-not $item -and -not $shot.button) {
            $items = $shots.OpenMenuPath($main, $path)
            $last = $items[$items.Count - 1]
            if ($shots.IsEnabled($last)) { $item = $last; break }
            $shots.CloseMenus($items)
            if ($clock.Elapsed.TotalSeconds -gt $OpenTimeoutSeconds) { throw "'$($shot.menu)' stayed disabled; does it need an image the run did not open?" }
            Start-Sleep -Milliseconds 1000
        }
        # A view action opens nothing: the view redraws, and the next step finds it so.
        if ($shot.view) {
            if ($shot.button) { [void]$shots.PressMainButton($main, $shot.button, $OpenTimeoutSeconds * 1000) }
            else { [void]$shots.Choose($item) }
            $rested = $false
            $shots.SettledPicture($mainHandle, $SettleSeconds * 1000, [ref]$rested).Dispose()
            $shots.Note("chose '$($shot.menu)'")
            continue
        }
        $before = New-Object 'System.Collections.Generic.HashSet[IntPtr]'
        foreach ($window in $shots.Windows()) { [void]$before.Add($window.Handle) }
        if ($shot.button) { [void]$shots.PressMainButton($main, $shot.button, $OpenTimeoutSeconds * 1000) }
        else { [void]$shots.Choose($item) }

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

        if ($List -and -not $shot.prepare) {
            $rested = $false
            $shots.SettledPicture($window.Handle, $SettleSeconds * 1000, [ref]$rested).Dispose()
            foreach ($line in $shots.DescribeControls($window.Handle)) { Write-Output "    $line" }
        }
        # What -Set asks for, once the window has drawn what it opens with; each change is given time to land.
        if ($shot.changes.Count -gt 0) {
            $rested = $false
            $shots.SettledPicture($window.Handle, $SettleSeconds * 1000, [ref]$rested).Dispose()
            $focused = $shots.Focused()
            foreach ($change in $shot.changes) {
                if ($change.ContainsKey('answer')) {
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
            # The keyboard goes back where it was, or to the window's Reset or Close button when no control had it
            # and the window itself did: a button given the keyboard without a key draws as it did.
            if (-not $focused -or $focused.Current.ControlType -eq [System.Windows.Automation.ControlType]::Window) {
                $focused = $shots.RestingButton($window.Handle)
            }
            $shots.Focus($focused)
            Start-Sleep -Milliseconds 600
        }

        # A preparation applies what it set and is no picture: the captures find the image as it leaves it.
        if ($shot.prepare) {
            $rested = $false
            $shots.SettledPicture($window.Handle, $SettleSeconds * 1000, [ref]$rested).Dispose()
            [void]$shots.Press($window.Handle, 'Apply')
            $shots.WaitForRun($window.Handle, 'Reset', 3000, $OpenTimeoutSeconds * 1000)
            $shots.SettledPicture($mainHandle, $SettleSeconds * 1000, [ref]$rested).Dispose()
            $shots.Note("applied '$($shot.menu)'")
        }
        else {
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
        }

        # The pair: the main window before the window's Apply and after its run. The window disables its Reset
        # button for as long as a run lasts.
        if ($Apply -and -not $shot.prepare) {
            $moved = $false
            $pointer = $shots.MovePointerOut($mainHandle, [ref]$moved)
            try {
                $rested = $false
                $viewBefore = $shots.SettledPicture($mainHandle, $SettleSeconds * 1000, [ref]$rested)
                $file = Join-Path $Output ($shot.name + '-before.png')
                $viewBefore.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
                $shots.Note("saved $file, $($viewBefore.Width) x $($viewBefore.Height)")
                [void]$shots.Press($window.Handle, 'Apply')
                $shots.WaitForRun($window.Handle, 'Reset', 3000, $OpenTimeoutSeconds * 1000)
                $viewAfter = $shots.SettledPicture($mainHandle, $SettleSeconds * 1000, [ref]$rested)
                $same = $shots.SamePicture($viewBefore, $viewAfter)
                $viewBefore.Dispose()
                if ($same) { $viewAfter.Dispose(); throw "Apply changed nothing in the main window" }
                $file = Join-Path $Output ($shot.name + '-after.png')
                $viewAfter.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
                $shots.Note("saved $file, $($viewAfter.Width) x $($viewAfter.Height)")
                $viewAfter.Dispose()
            }
            finally {
                if ($moved) { $shots.PutPointerBack($pointer) }
            }
        }

        # What the window's run left on the screen - a message that it is complete, its progress window - is
        # dismissed first, the last one shown first: Return answers a message, Escape closes a window that is done.
        for ($pass = 0; $pass -lt 4; ++$pass) {
            $leftOpen = @($shots.Windows() | Where-Object { $_.Handle -ne $window.Handle -and -not $before.Contains($_.Handle) -and $_.Title -and $_.ClassName.EndsWith('QWindowIcon') })
            if ($leftOpen.Count -eq 0) { break }
            $shots.Note("dismissing '$($leftOpen[0].Title)'")
            if ($shots.HasMinimizeButton($leftOpen[0].Handle)) { $shots.PostKey($leftOpen[0].Handle, 0x1B) } else { $shots.PostKey($leftOpen[0].Handle, 0x0D) }
            Start-Sleep -Milliseconds 700
            if ($shots.IsShown($leftOpen[0].Handle)) { $shots.PostKey($leftOpen[0].Handle, 0x1B); Start-Sleep -Milliseconds 700 }
        }

        # Escape closes a process window; a window that stays is asked to close.
        $shots.PostKey($window.Handle, 0x1B)
        $clock.Restart()
        while ($shots.IsShown($window.Handle) -and $clock.ElapsedMilliseconds -lt 5000) { Start-Sleep -Milliseconds 100 }
        if ($shots.IsShown($window.Handle)) {
            $shots.Close($window.Handle)
            $clock.Restart()
            while ($shots.IsShown($window.Handle) -and $clock.ElapsedMilliseconds -lt 5000) { Start-Sleep -Milliseconds 100 }
        }
        if ($shots.IsShown($window.Handle)) {
            # A window that will not close is usually behind another one of the application's, which is named.
            $others = @($shots.Windows() | Where-Object { $_.Handle -ne $window.Handle -and $_.Handle -ne $mainHandle -and $_.Title } | ForEach-Object { "'$($_.Title)'" })
            $beside = ''
            if ($others.Count -gt 0) { $beside = "; the application also shows $($others -join ', ')" }
            throw "'$($window.Title)' did not close$beside"
        }
        Start-Sleep -Milliseconds 500
    }

    if ($MainWindow) {
        # An image the captures left open - the result of a run - is shown as -Display says, as the opened ones were.
        if ($Display) {
            $box = $shots.ComboBox($main, $displayModeBox, 3000)
            if ($box -and $shots.ValueOf($box) -ne $Display) {
                if (-not $shots.ChooseInComboBox($mainHandle, $box, $Display)) { throw "the image's display mode has no '$Display'" }
                $shots.Note("the image is shown as $Display")
            }
        }
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
