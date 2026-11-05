<#
.SYNOPSIS
The product measurement's application driver on Windows: one ImageIntegration run through the application's own window.

.DESCRIPTION
The product measurement runs the product's workflow two ways: through AfterNightWorkflowDriver, which runs the
workflow's orchestration with no window, and through the application itself - its menus, its process windows, its
progress dialog - which is the path a person takes. This script is the second. It starts a deployment's application with
a storage root of its own, opens Image Integration or Image Integration Lite from the Process menu, adds each role's
frames from a folder through the application's own folder dialog, names the output directory, asks for the run, and
watches for the window that says the run ended, while a sampler reads the process's working set every few milliseconds.
It then stops the application and writes a result shaped like the workflow driver's, so product_measurement.py pairs,
compares and decides both alike.

It works the same against the baseline and the candidate deployment, because it drives only the windows, object names
and dialogs both keep, and it modifies neither: an archived deployment is run as it was built.

The measured region is the one a person sees: from the request - Run > Execute Integration in the full window, Run
Integration in Lite - to the message that ends the progress dialog. In the full window the preparation ends when the
execution plan is shown and the execution starts when its Run is pressed; the time the plan waits for that press is the
driver's, not the product's, and is left out. The resident peak is the largest working set sampled inside the region, or
the process's high-water mark when it rose inside the region. The engine's own accounting is read from the run's log,
where the workflow executor writes it.

Usage (Windows PowerShell 5.1, which carries UI Automation):
    powershell -NoProfile -ExecutionPolicy Bypass -File product_application_driver.ps1 -Application <AfterNight.exe>
        -Workflow full|lite -Frames <dir with BIAS DARK FLAT LIGHTS> -Output <workflow output dir> -Storage <new dir>
        -Result <result.json> [-Variant 0|1] [-CancelAfterMs N] [-SamplingIntervalMs N] [-DatasetIdentity <sha256>]
        [-TimeoutSeconds N]

The Linux equivalent is product_application_driver_linux.py, which drives the same windows through AT-SPI and writes
the same result; product_measurement.py --path application launches either.
#>
param(
    [Parameter(Mandatory = $true)][string]$Application,
    [Parameter(Mandatory = $true)][ValidateSet('full', 'lite')][string]$Workflow,
    [Parameter(Mandatory = $true)][string]$Frames,
    [Parameter(Mandatory = $true)][string]$Output,
    [Parameter(Mandatory = $true)][string]$Storage,
    [Parameter(Mandatory = $true)][string]$Result,
    [ValidateRange(0, 1)][int]$Variant = 0,
    [int]$CancelAfterMs = 0,
    [int]$SamplingIntervalMs = 10,
    [string]$DatasetIdentity = '',
    [int]$TimeoutSeconds = 1800
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$source = @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Automation;

public static class AnDriverNative
{
    public delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr dialog, int id);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessageTimeout(IntPtr hwnd, uint message, IntPtr wParam, string lParam, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    public struct MemoryCounters
    {
        public uint cb;
        public uint PageFaultCount;
        public UIntPtr PeakWorkingSetSize;
        public UIntPtr WorkingSetSize;
        public UIntPtr QuotaPeakPagedPoolUsage;
        public UIntPtr QuotaPagedPoolUsage;
        public UIntPtr QuotaPeakNonPagedPoolUsage;
        public UIntPtr QuotaNonPagedPoolUsage;
        public UIntPtr PagefileUsage;
        public UIntPtr PeakPagefileUsage;
        public UIntPtr PrivateUsage;
    }
    [DllImport("psapi.dll", SetLastError = true)]
    public static extern bool GetProcessMemoryInfo(IntPtr process, out MemoryCounters counters, uint size);
    [DllImport("kernel32.dll")]
    public static extern bool GetProcessTimes(IntPtr process, out long creation, out long exit, out long kernel, out long user);
}

/// One top-level window of the process.
public sealed class AnWindow
{
    public IntPtr Handle;
    public string Title;
    public string ClassName;
}

/// The working set of the process, read every interval on a thread of its own.
public sealed class AnSampler
{
    private readonly IntPtr m_process;
    private readonly int m_intervalMs;
    private readonly List<long> m_times = new List<long>();
    private readonly List<ulong> m_bytes = new List<ulong>();
    private readonly object m_lock = new object();
    private volatile bool m_stop;
    private Thread m_thread;

    public AnSampler(IntPtr process, int intervalMs)
    {
        m_process = process;
        m_intervalMs = Math.Max(1, intervalMs);
    }

    public void Start()
    {
        m_thread = new Thread(Run);
        m_thread.IsBackground = true;
        m_thread.Priority = ThreadPriority.AboveNormal;
        m_thread.Start();
    }

    public void Stop()
    {
        m_stop = true;
        if (m_thread != null) m_thread.Join();
    }

    private void Run()
    {
        while (!m_stop)
        {
            AnDriverNative.MemoryCounters counters;
            if (AnDriverNative.GetProcessMemoryInfo(m_process, out counters, (uint)Marshal.SizeOf(typeof(AnDriverNative.MemoryCounters))))
            {
                lock (m_lock)
                {
                    m_times.Add(Stopwatch.GetTimestamp());
                    m_bytes.Add(counters.WorkingSetSize.ToUInt64());
                }
            }
            Thread.Sleep(m_intervalMs);
        }
    }

    /// The largest reading between two timestamps, and how many readings fell between them.
    public ulong PeakBetween(long from, long to, out int count)
    {
        ulong peak = 0;
        count = 0;
        lock (m_lock)
        {
            for (int i = 0; i < m_times.Count; ++i)
            {
                if (m_times[i] < from || m_times[i] > to) continue;
                ++count;
                if (m_bytes[i] > peak) peak = m_bytes[i];
            }
        }
        return peak;
    }
}

/// Drives one run through the application's window and measures it.
public sealed class AnApplicationDriver
{
    private readonly Process m_process;
    private readonly uint m_pid;
    public readonly List<string> Events = new List<string>();
    private readonly long m_origin = Stopwatch.GetTimestamp();

    public AnApplicationDriver(Process process)
    {
        m_process = process;
        m_pid = (uint)process.Id;
    }

    public static double Milliseconds(long from, long to)
    {
        return (to - from) * 1000.0 / Stopwatch.Frequency;
    }

    public void Note(string text)
    {
        string line = string.Format("{0,9:F1} ms {1}", Milliseconds(m_origin, Stopwatch.GetTimestamp()), text);
        Events.Add(line);
        Console.WriteLine(line);
    }

    public void CheckAlive()
    {
        if (m_process.HasExited) throw new InvalidOperationException("the application exited with code " + m_process.ExitCode);
    }

    // ---- Top-level windows, read through the window manager: cheap enough to poll inside the measured region.

    public List<AnWindow> Windows()
    {
        List<AnWindow> found = new List<AnWindow>();
        uint pid = m_pid;
        AnDriverNative.EnumWindows(delegate(IntPtr hwnd, IntPtr unused)
        {
            uint owner;
            AnDriverNative.GetWindowThreadProcessId(hwnd, out owner);
            if (owner != pid || !AnDriverNative.IsWindowVisible(hwnd)) return true;
            StringBuilder title = new StringBuilder(512);
            AnDriverNative.GetWindowText(hwnd, title, 512);
            StringBuilder cls = new StringBuilder(256);
            AnDriverNative.GetClassName(hwnd, cls, 256);
            AnWindow window = new AnWindow();
            window.Handle = hwnd;
            window.Title = title.ToString();
            window.ClassName = cls.ToString();
            found.Add(window);
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public AnWindow FindWindow(string[] titles)
    {
        foreach (AnWindow window in Windows())
        {
            foreach (string title in titles)
            {
                if (window.Title == title) return window;
            }
        }
        return null;
    }

    public AnWindow WaitWindow(string[] titles, int timeoutMs, int pollMs)
    {
        Stopwatch clock = Stopwatch.StartNew();
        while (clock.ElapsedMilliseconds < timeoutMs)
        {
            AnWindow window = FindWindow(titles);
            if (window != null) return window;
            CheckAlive();
            Thread.Sleep(pollMs);
        }
        return null;
    }

    public bool WaitWindowGone(string[] titles, int timeoutMs)
    {
        Stopwatch clock = Stopwatch.StartNew();
        while (clock.ElapsedMilliseconds < timeoutMs)
        {
            if (FindWindow(titles) == null) return true;
            CheckAlive();
            Thread.Sleep(50);
        }
        return false;
    }

    public AnWindow NativeDialog()
    {
        foreach (AnWindow window in Windows())
        {
            if (window.ClassName == "#32770") return window;
        }
        return null;
    }

    /// Answers the operating system's file or folder dialog with a path: its name box is given the path and the dialog
    /// is accepted, again if the first acceptance only opened the folder.
    public void AnswerNativeDialog(string path, int timeoutMs)
    {
        Stopwatch clock = Stopwatch.StartNew();
        AnWindow dialog = null;
        while (dialog == null && clock.ElapsedMilliseconds < timeoutMs)
        {
            dialog = NativeDialog();
            if (dialog == null) { CheckAlive(); Thread.Sleep(50); }
        }
        if (dialog == null) throw new TimeoutException("no file dialog opened");
        Note("dialog '" + dialog.Title + "' for " + path);
        Thread.Sleep(300);
        IntPtr edit = AnDriverNative.GetDlgItem(dialog.Handle, 1152);
        if (edit == IntPtr.Zero) edit = AnDriverNative.GetDlgItem(dialog.Handle, 1148);
        if (edit == IntPtr.Zero) throw new InvalidOperationException("the file dialog has no name box");
        IntPtr ignored;
        AnDriverNative.SendMessageTimeout(edit, 0x000C, IntPtr.Zero, path, 2, 5000, out ignored);
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            AnDriverNative.PostMessage(dialog.Handle, 0x0111, (IntPtr)1, IntPtr.Zero);
            Stopwatch wait = Stopwatch.StartNew();
            while (wait.ElapsedMilliseconds < 2000)
            {
                if (!StillOpen(dialog.Handle)) return;
                Thread.Sleep(50);
            }
            // The first acceptance of a folder's path opens the folder; accepting again selects it.
            AnDriverNative.SendMessageTimeout(edit, 0x000C, IntPtr.Zero, "", 2, 5000, out ignored);
        }
        throw new TimeoutException("the file dialog did not accept " + path);
    }

    private bool StillOpen(IntPtr handle)
    {
        foreach (AnWindow window in Windows())
        {
            if (window.Handle == handle) return true;
        }
        return false;
    }

    /// Closes a message box the application shows, by its title, if it is open; answers it with its default.
    public bool DismissBox(string title)
    {
        AnWindow box = FindWindow(new string[] { title });
        if (box == null) return false;
        AutomationElement element = AutomationElement.FromHandle(box.Handle);
        AutomationElement button = element.FindFirst(TreeScope.Descendants,
            new AndCondition(new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.Button),
                             new PropertyCondition(AutomationElement.NameProperty, "OK")));
        if (button != null)
        {
            InvokeAsync(button);
        }
        else
        {
            AnDriverNative.PostMessage(box.Handle, 0x0010, IntPtr.Zero, IntPtr.Zero);
        }
        Note("dismissed '" + title + "'");
        return true;
    }

    // ---- The application's widgets, read through UI Automation: used to set the run up, and once to start or stop it.

    private Condition ProcessCondition()
    {
        return new PropertyCondition(AutomationElement.ProcessIdProperty, (int)m_pid);
    }

    public AutomationElement WaitElement(Func<AutomationElement> find, string what, int timeoutMs)
    {
        Stopwatch clock = Stopwatch.StartNew();
        while (clock.ElapsedMilliseconds < timeoutMs)
        {
            AutomationElement element = null;
            try { element = find(); } catch (ElementNotAvailableException) { element = null; }
            if (element != null) return element;
            CheckAlive();
            Thread.Sleep(200);
        }
        throw new TimeoutException(what + " did not appear");
    }

    public AutomationElement ById(AutomationElement scope, string automationIdSuffix, ControlType type)
    {
        AutomationElementCollection all = scope.FindAll(TreeScope.Descendants,
            new PropertyCondition(AutomationElement.ControlTypeProperty, type));
        foreach (AutomationElement element in all)
        {
            AutomationElement.AutomationElementInformation current = element.Current;
            if (current.AutomationId.EndsWith(automationIdSuffix) && !current.IsOffscreen) return element;
        }
        return null;
    }

    public AutomationElement ByName(AutomationElement scope, string name, ControlType type)
    {
        AutomationElementCollection all = scope.FindAll(TreeScope.Descendants,
            new AndCondition(new PropertyCondition(AutomationElement.ControlTypeProperty, type),
                             new PropertyCondition(AutomationElement.NameProperty, name)));
        foreach (AutomationElement element in all)
        {
            if (!element.Current.IsOffscreen) return element;
        }
        return all.Count > 0 ? all[0] : null;
    }

    public AutomationElement TopLevel(string automationId)
    {
        return AutomationElement.RootElement.FindFirst(TreeScope.Children,
            new AndCondition(ProcessCondition(), new PropertyCondition(AutomationElement.AutomationIdProperty, automationId)));
    }

    public AutomationElement Window(string automationId, int timeoutMs)
    {
        string id = automationId;
        return WaitElement(delegate
        {
            AutomationElement found = TopLevel(id);
            if (found != null) return found;
            AutomationElement main = TopLevel("QApplication.afterNightMainWindow");
            return main == null ? null : main.FindFirst(TreeScope.Children,
                new PropertyCondition(AutomationElement.AutomationIdProperty, id));
        }, automationId, timeoutMs);
    }

    /// Invokes a control on a thread of its own: a control whose action opens a modal dialog does not return until
    /// the dialog closes.
    public Task InvokeAsync(AutomationElement element)
    {
        return Task.Run(delegate
        {
            try
            {
                ((InvokePattern)element.GetCurrentPattern(InvokePattern.Pattern)).Invoke();
            }
            catch (Exception)
            {
            }
        });
    }

    /// Opens a menu path - each item expanded, the last one found - anywhere in the process, and returns the last item
    /// for the caller to invoke, so a request is timed from the invocation rather than from the search.
    public AutomationElement Menu(AutomationElement scope, string[] path)
    {
        for (int i = 0; i < path.Length; ++i)
        {
            string name = path[i];
            AutomationElement root = i == 0 ? scope : AutomationElement.RootElement;
            AutomationElement item = WaitElement(delegate
            {
                return root.FindFirst(TreeScope.Descendants, new AndCondition(ProcessCondition(),
                    new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.MenuItem),
                    new PropertyCondition(AutomationElement.NameProperty, name)));
            }, "menu item " + name, 10000);
            if (i == path.Length - 1)
            {
                return item;
            }
            ((ExpandCollapsePattern)item.GetCurrentPattern(ExpandCollapsePattern.Pattern)).Expand();
            Thread.Sleep(300);
        }
        return null;
    }

    public void SelectTab(AutomationElement scope, string name)
    {
        AutomationElement tab = WaitElement(delegate { return ByName(scope, name, ControlType.TabItem); }, "tab " + name, 10000);
        ((SelectionItemPattern)tab.GetCurrentPattern(SelectionItemPattern.Pattern)).Select();
        Thread.Sleep(300);
    }

    public string ValueOf(AutomationElement element)
    {
        object pattern;
        if (element.TryGetCurrentPattern(ValuePattern.Pattern, out pattern)) return ((ValuePattern)pattern).Current.Value;
        return element.Current.Name;
    }

    // ---- What the process has used.

    public void Memory(out ulong workingSet, out ulong peakWorkingSet, out ulong privateBytes)
    {
        AnDriverNative.MemoryCounters counters;
        AnDriverNative.GetProcessMemoryInfo(m_process.Handle, out counters, (uint)Marshal.SizeOf(typeof(AnDriverNative.MemoryCounters)));
        workingSet = counters.WorkingSetSize.ToUInt64();
        peakWorkingSet = counters.PeakWorkingSetSize.ToUInt64();
        privateBytes = counters.PrivateUsage.ToUInt64();
    }

    public double CpuSeconds()
    {
        long creation, exit, kernel, user;
        if (!AnDriverNative.GetProcessTimes(m_process.Handle, out creation, out exit, out kernel, out user)) return -1.0;
        return (kernel + user) / 1e7;
    }
}
'@

Add-Type -TypeDefinition $source -ReferencedAssemblies UIAutomationClient, UIAutomationTypes, WindowsBase -Language CSharp

function Ms([long]$from, [long]$to) { [AnApplicationDriver]::Milliseconds($from, $to) }

$frameRoles = @(
    @{ tab = 'Bias'; folder = 'BIAS' },
    @{ tab = 'Darks'; folder = 'DARK' },
    @{ tab = 'Flats'; folder = 'FLAT' },
    @{ tab = 'Lights'; folder = 'LIGHTS' }
)
$completionTitles = [string[]]@('Complete', 'Completed with Errors', 'Cancelled', 'Failed')
$refusalTitles = [string[]]@('Execution Error', 'Nothing to Process', 'Registration Records', 'Import Failed',
    'No Files Found')

$Application = (Resolve-Path $Application).Path
$Frames = (Resolve-Path $Frames).Path
New-Item -ItemType Directory -Force -Path $Output | Out-Null
New-Item -ItemType Directory -Force -Path $Storage | Out-Null
$Output = (Resolve-Path $Output).Path
$Storage = (Resolve-Path $Storage).Path

$deployment = Split-Path $Application
$start = New-Object System.Diagnostics.ProcessStartInfo
$start.FileName = $Application
$start.WorkingDirectory = $deployment
$start.UseShellExecute = $false
$systemRoot = $env:SystemRoot
$start.EnvironmentVariables['PATH'] = "$deployment;$systemRoot\System32;$systemRoot"
$start.EnvironmentVariables['AFTERNIGHT_TEST_STORAGE_ROOT'] = $Storage
foreach ($name in @('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM', 'PYTHONHOME', 'PYTHONPATH')) {
    if ($start.EnvironmentVariables.ContainsKey($name)) { $start.EnvironmentVariables.Remove($name) }
}

$record = [ordered]@{
    schema      = 1
    driver      = [ordered]@{ kind = 'application'; application = $Application; workflow = $Workflow; variant = $Variant
        samplingIntervalMs = $SamplingIntervalMs; cancelAfterMs = $CancelAfterMs }
    dataset     = [ordered]@{ identityDigest = $DatasetIdentity }
    outcome     = [ordered]@{ success = $false; cancelled = $false; cancellationLatencyMs = $null; completion = $null
        error = $null }
    timing      = [ordered]@{ totalMs = $null; preparationMs = $null; executionMs = $null; stages = @() }
    memory      = [ordered]@{}
    process     = [ordered]@{}
    preparation = [ordered]@{ executionMode = $null }
    engine      = $null
    outputs     = @()
    events      = @()
}

[uint64]$idle = 0; [uint64]$peakBefore = 0; [uint64]$privateBefore = 0
[uint64]$workingSetAtEnd = 0; [uint64]$peakAfter = 0; [uint64]$privateAfter = 0; [int]$count = 0
$process = [System.Diagnostics.Process]::Start($start)
$driver = New-Object AnApplicationDriver $process
$sampler = New-Object AnSampler $process.Handle, $SamplingIntervalMs
$sampler.Start()
$acceptedWallClock = $null
try {
    $driver.Note("started $Application, pid $($process.Id)")
    $main = $driver.Window('QApplication.afterNightMainWindow', 120000)
    $driver.Note('main window shown')
    Start-Sleep -Milliseconds 1500

    if ($Workflow -eq 'full') {
        [void]$driver.InvokeAsync($driver.Menu($main, [string[]]@('Process', 'Preprocess', 'Image Integration')))
        $window = $driver.Window('QApplication.ImageIntegrationProcess', 60000)
        $driver.Note('Image Integration shown')
        foreach ($role in $frameRoles) {
            $driver.SelectTab($window, $role.tab)
            $button = $driver.WaitElement({ $driver.ByName($window, 'Add Folder', [System.Windows.Automation.ControlType]::Button) },
                "Add Folder on $($role.tab)", 10000)
            [void]$driver.InvokeAsync($button)
            $driver.AnswerNativeDialog((Join-Path $Frames $role.folder), 20000)
            # The import runs behind a progress dialog, and a box of its own may then report it.
            Start-Sleep -Milliseconds 500
            [void]$driver.WaitWindowGone([string[]]@('Importing files...'), 600000)
            $box = $driver.WaitWindow([string[]]@('Import Complete', 'Import Issues'), 1500, 100)
            if ($box) { [void]$driver.DismissBox($box.Title); Start-Sleep -Milliseconds 500 }
            $refusal = $driver.FindWindow($refusalTitles)
            if ($refusal) { throw "the import said '$($refusal.Title)'" }
            $driver.Note("$($role.tab) imported")
        }
        $browse = $driver.WaitElement({ $driver.ById($window, 'outputDirectoryBrowseButton', [System.Windows.Automation.ControlType]::Button) },
            'the output directory button', 10000)
        [void]$driver.InvokeAsync($browse)
        $driver.AnswerNativeDialog($Output, 20000)
        Start-Sleep -Milliseconds 500
        $edit = $driver.ById($window, 'outputDirectoryEdit', [System.Windows.Automation.ControlType]::Edit)
        $shown = $driver.ValueOf($edit)
        if ([System.IO.Path]::GetFullPath($shown).TrimEnd('\') -ne $Output.TrimEnd('\')) {
            throw "the output directory reads '$shown', not '$Output'"
        }
        if ($Variant -ne 0) {
            # The incremental case changes the light stack alone, as a person changes it: the group's settings on the
            # Stacking tab, its high rejection sigma 0.1 above what the window shows.
            $driver.SelectTab($window, 'Workflow')
            $driver.SelectTab($window, 'Stacking')
            $table = $driver.WaitElement({ $driver.ById($window, 'tblIntegrationGroups', [System.Windows.Automation.ControlType]::Table) },
                'the integration groups', 10000)
            $cell = $driver.WaitElement({ $table.FindFirst([System.Windows.Automation.TreeScope]::Descendants,
                        (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty,
                            [System.Windows.Automation.ControlType]::DataItem))) }, 'an integration group', 10000)
            ([System.Windows.Automation.SelectionItemPattern]$cell.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)).Select()
            Start-Sleep -Milliseconds 300
            $settings = $driver.WaitElement({ $driver.ById($window, 'btnStackingSettings', [System.Windows.Automation.ControlType]::Button) },
                'the stacking settings button', 10000)
            [void]$driver.InvokeAsync($settings)
            $dialog = $driver.WaitWindow([string[]]@('LIGHT Integration Stacking Settings'), 20000, 50)
            if (-not $dialog) { throw 'the light stacking settings did not open' }
            $dialogElement = [System.Windows.Automation.AutomationElement]::FromHandle($dialog.Handle)
            $sigma = $driver.WaitElement({ $driver.ById($dialogElement, 'sigmaHighValue', [System.Windows.Automation.ControlType]::Spinner) },
                'the high sigma', 10000)
            $range = [System.Windows.Automation.RangeValuePattern]$sigma.GetCurrentPattern([System.Windows.Automation.RangeValuePattern]::Pattern)
            $default = $range.Current.Value
            $range.SetValue([Math]::Round($default + 0.1, 2))
            Start-Sleep -Milliseconds 300
            $driver.Note("high sigma $default -> $($range.Current.Value)")
            $ok = $driver.WaitElement({ $driver.ByName($dialogElement, 'OK', [System.Windows.Automation.ControlType]::Button) },
                'the settings'' OK', 10000)
            [void]$driver.InvokeAsync($ok)
            if (-not $driver.WaitWindowGone([string[]]@('LIGHT Integration Stacking Settings'), 20000)) { throw 'the settings did not close' }
        }
        Start-Sleep -Milliseconds 1000

        $execute = $driver.Menu($window, [string[]]@('Run', 'Execute Integration'))
        $driver.Memory([ref]$idle, [ref]$peakBefore, [ref]$privateBefore)
        $cpuBefore = $driver.CpuSeconds()
        $requested = [System.Diagnostics.Stopwatch]::GetTimestamp()
        [void]$driver.InvokeAsync($execute)
        $plan = $driver.WaitWindow([string[]]@('Execution Plan') + $refusalTitles, 3600000, 5)
        $planned = [System.Diagnostics.Stopwatch]::GetTimestamp()
        if (-not $plan -or $plan.Title -ne 'Execution Plan') { throw "the request ended in '$($plan.Title)'" }
        $driver.Note('execution plan shown')
        $planElement = [System.Windows.Automation.AutomationElement]::FromHandle($plan.Handle)
        $run = $driver.WaitElement({ $driver.ByName($planElement, 'Run', [System.Windows.Automation.ControlType]::Button) },
            'the plan''s Run button', 10000)
        $accepted = [System.Diagnostics.Stopwatch]::GetTimestamp()
        $acceptedWallClock = [DateTime]::Now
        [void]$driver.InvokeAsync($run)
    }
    else {
        [void]$driver.InvokeAsync($driver.Menu($main, [string[]]@('Process', 'Preprocess', 'Image Integration Lite')))
        $window = $driver.Window('QApplication.ImageIntegrationLiteProcess', 60000)
        $driver.Note('Image Integration Lite shown')
        if ($Variant -ne 0) { throw 'the Lite window has no driven variant yet' }
        $edit = $driver.WaitElement({ $driver.ById($window, 'liteOutputDirectoryEdit', [System.Windows.Automation.ControlType]::Edit) },
            'the output directory', 10000)
        ([System.Windows.Automation.ValuePattern]$edit.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)).SetValue($Output)
        # Lite adds a folder and every folder below it, and reads each frame's kind from its cards and folders; the
        # import is done when its summary counts every frame of every role.
        $counts = @{}
        foreach ($role in $frameRoles) {
            $counts[$role.folder] = @(Get-ChildItem -LiteralPath (Join-Path $Frames $role.folder) -File).Count
        }
        $expected = '{0} light, {1} flat, {2} dark, {3} bias' -f $counts['LIGHTS'], $counts['FLAT'], $counts['DARK'], $counts['BIAS']
        $add = $driver.WaitElement({ $driver.ById($window, 'liteAddFolderButton', [System.Windows.Automation.ControlType]::Button) },
            'Add Folder', 10000)
        [void]$driver.InvokeAsync($add)
        $driver.AnswerNativeDialog($Frames, 20000)
        [void]$driver.WaitElement({
                $label = $driver.ById($window, 'liteFrameSummaryLabel', [System.Windows.Automation.ControlType]::Text)
                if ($label -and $label.Current.Name -eq $expected) { $label } else { $null }
            }, "the frame summary '$expected'", 600000)
        $driver.Note("imported: $expected")
        [void]$driver.WaitElement({
                $button = $driver.ById($window, 'liteRunIntegrationButton', [System.Windows.Automation.ControlType]::Button)
                if ($button -and $button.Current.IsEnabled) { $button } else { $null }
            }, 'an enabled Run Integration', 60000)
        Start-Sleep -Milliseconds 1000

        $driver.Memory([ref]$idle, [ref]$peakBefore, [ref]$privateBefore)
        $cpuBefore = $driver.CpuSeconds()
        $runButton = $driver.WaitElement({ $driver.ById($window, 'liteRunIntegrationButton', [System.Windows.Automation.ControlType]::Button) },
            'Run Integration', 10000)
        $requested = [System.Diagnostics.Stopwatch]::GetTimestamp()
        $planned = $null
        $accepted = $requested
        $acceptedWallClock = [DateTime]::Now
        [void]$driver.InvokeAsync($runButton)
    }

    $driver.Note('run requested')
    $cancelRequested = $null
    $deadline = $accepted + [long]$TimeoutSeconds * [System.Diagnostics.Stopwatch]::Frequency
    $ended = $null
    while (-not $ended) {
        $now = [System.Diagnostics.Stopwatch]::GetTimestamp()
        if ($now -gt $deadline) { throw "the run did not end within $TimeoutSeconds s" }
        if ($CancelAfterMs -gt 0 -and -not $cancelRequested -and (Ms $accepted $now) -ge $CancelAfterMs) {
            $progress = $driver.FindWindow([string[]]@('Image Integration - Processing'))
            if (-not $progress) { throw 'no progress dialog to cancel' }
            $progressElement = [System.Windows.Automation.AutomationElement]::FromHandle($progress.Handle)
            $cancel = $driver.ById($progressElement, 'btnCancel', [System.Windows.Automation.ControlType]::Button)
            if (-not $cancel) { throw 'the progress dialog has no Cancel button' }
            $cancelRequested = [System.Diagnostics.Stopwatch]::GetTimestamp()
            [void]$driver.InvokeAsync($cancel)
            $driver.Note('cancellation requested')
        }
        $ended = $driver.FindWindow($completionTitles)
        if (-not $ended) {
            $driver.CheckAlive()
            Start-Sleep -Milliseconds 5
        }
    }
    $finished = [System.Diagnostics.Stopwatch]::GetTimestamp()
    $driver.Memory([ref]$workingSetAtEnd, [ref]$peakAfter, [ref]$privateAfter)
    $cpuAfter = $driver.CpuSeconds()
    $driver.Note("run ended: $($ended.Title)")

    $sampledPeak = $sampler.PeakBetween($requested, $finished, [ref]$count)
    if ($peakAfter -gt $peakBefore) {
        $residentPeak = $peakAfter
        $peakSource = 'the process''s high-water mark, which rose inside the region'
    }
    else {
        $residentPeak = $sampledPeak
        $peakSource = 'the sampler''s largest reading'
    }
    $record.outcome.success = ($ended.Title -eq 'Complete' -or $ended.Title -eq 'Completed with Errors') -and -not $cancelRequested
    $record.outcome.cancelled = ($ended.Title -eq 'Cancelled')
    $record.outcome.completion = $ended.Title
    if ($cancelRequested) { $record.outcome.cancellationLatencyMs = Ms $cancelRequested $finished }
    $record.timing.executionMs = Ms $accepted $finished
    if ($planned) {
        $record.timing.preparationMs = Ms $requested $planned
        $record.timing.totalMs = $record.timing.preparationMs + $record.timing.executionMs
        $record.timing.planWaitMs = Ms $planned $accepted
    }
    else {
        $record.timing.totalMs = $record.timing.executionMs
    }
    $record.memory = [ordered]@{
        residentPeakBytes   = $residentPeak
        residentPeakSource  = $peakSource
        sampledPeakBytes    = $sampledPeak
        samplesInRegion     = $count
        idleBaselineBytes   = $idle
        workingSetAtEndBytes = $workingSetAtEnd
        lifetimePeakBeforeRegionBytes = $peakBefore
        lifetimePeakBytes   = $peakAfter
        privateBytesBeforeRegion = $privateBefore
        privateBytesAtEnd   = $privateAfter
        metric              = 'the process working set (GetProcessMemoryInfo), sampled from outside the process'
    }
    $record.process.cpuSecondsInRegion = $cpuAfter - $cpuBefore
    $record.process.cpuSeconds = $cpuAfter
}
catch {
    $record.outcome.error = $_.Exception.Message
    $driver.Note("refused: $($_.Exception.Message)")
}
finally {
    $sampler.Stop()
    if (-not $process.HasExited) {
        & taskkill.exe /PID $process.Id /T /F | Out-Null
        $process.WaitForExit(30000) | Out-Null
    }
}

# What the run left behind: its log, the images it published, and the stages and engine accounting its log records.
# Only a log the run wrote after its request is its own; an output directory keeps every earlier run's.
$logs = @(Get-ChildItem -Path (Join-Path $Output 'history') -Filter 'workflow_log.txt' -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $acceptedWallClock -and $_.LastWriteTime -ge $acceptedWallClock.AddSeconds(-2) } | Sort-Object LastWriteTime)
if ($logs.Count -gt 0) {
    $log = Get-Content -LiteralPath $logs[-1].FullName -Encoding UTF8
    # Each stage's wall time, from the timestamps the run's log gives its start and each stage's completion.
    $completed = @{}
    $started = $null
    $previous = $null
    foreach ($line in $log) {
        if ($line -match '^\[([^\]]+)\] \[HEADER\] WORKFLOW STARTED') {
            $started = [DateTime]::Parse($Matches[1]); $previous = $started
        }
        elseif ($started -and $line -match '^\[([^\]]+)\] \[SUCCESS\] \S+ (.+?) Complete \S+ \(') {
            $at = [DateTime]::Parse($Matches[1])
            $completed[($Matches[2] -replace ' ', '')] = [ordered]@{ ms = ($at - $previous).TotalMilliseconds
                endMs = ($at - $started).TotalMilliseconds }
            $previous = $at
        }
    }
    $stages = @()
    foreach ($line in $log) {
        if ($line -match 'STAGE: (.+?) \(\d+ group') {
            $name = $Matches[1] -replace ' ', ''
            $stage = [ordered]@{ name = $name }
            if ($completed.ContainsKey($name)) { $stage.ms = $completed[$name].ms; $stage.endMs = $completed[$name].endMs }
            $stages += $stage
        }
        if ($line -match 'Engine memory: peak (\d+) bytes \((\d+) when the run started\), (\d+) bytes still allocated, (\d+) allocations of (\d+) bytes, (\d+) refused') {
            $record.engine = [ordered]@{ region = [ordered]@{ peakBytes = [uint64]$Matches[1]; peakBeforeRegionBytes = [uint64]$Matches[2]
                    liveBytesAfterRegion = [uint64]$Matches[3]; allocationsInRegion = [uint64]$Matches[4]
                    bytesAllocatedInRegion = [uint64]$Matches[5]; deniedAllocationsInRegion = [uint64]$Matches[6] } }
        }
    }
    $record.timing.stages = $stages
    $record.workflowLog = $logs[-1].FullName.Substring($Output.Length).TrimStart('\').Replace('\', '/')
}
$applicationLog = Join-Path $Storage 'logs\AfterNight.log'
if (Test-Path -LiteralPath $applicationLog) {
    $record.preparation.executionMode = if (Select-String -LiteralPath $applicationLog -SimpleMatch 'starting incremental execution' -Quiet) { 'incremental' } else { 'full' }
}
if ($acceptedWallClock) {
    $roles = @{ 'master_bias_' = 'bias-master'; 'master_dark_' = 'dark-master'; 'master_flat_' = 'flat-master'; 'stacked_' = 'light-master' }
    $published = @()
    foreach ($file in (Get-ChildItem -LiteralPath $Output -File | Where-Object { $_.Extension -in '.fit', '.fits', '.fts' } | Sort-Object Name)) {
        if ($file.LastWriteTime -lt $acceptedWallClock.AddSeconds(-2)) { continue }
        $role = $null
        foreach ($prefix in $roles.Keys) { if ($file.Name.StartsWith($prefix)) { $role = $roles[$prefix] } }
        if (-not $role) { continue }
        $published += [ordered]@{ role = $role; path = $file.Name; bytes = $file.Length
            sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
    $record.outputs = $published
}
$record.events = @($driver.Events)
$json = $record | ConvertTo-Json -Depth 8
[System.IO.File]::WriteAllText($Result, $json + "`n", (New-Object System.Text.UTF8Encoding($false)))
if ($record.outcome.error) { exit 2 }
exit 0
