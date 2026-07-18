using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;

namespace AxiomJamesDSPController;

internal static class Program
{
    private const string InstanceMutexName = @"Local\AxiomJamesDSPController.SingleInstance";

    [STAThread]
    private static void Main()
    {
        using var instanceMutex = new Mutex(true, InstanceMutexName, out var ownsMutex);
        if (!ownsMutex)
        {
            NativeWindowActivation.ActivateExistingWindow("Axiom JamesDSP Controller");
            return;
        }

        ApplicationConfiguration.Initialize();
        Application.Run(new MainForm());
    }
}

internal static class NativeWindowActivation
{
    private const int SwRestore = 9;

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr FindWindow(string? className, string windowName);

    [DllImport("user32.dll")]
    private static extern bool IsIconic(IntPtr window);

    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr window, int command);

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr window);

    public static void ActivateExistingWindow(string title)
    {
        for (var attempt = 0; attempt < 10; attempt++)
        {
            var window = FindWindow(null, title);
            if (window != IntPtr.Zero)
            {
                if (IsIconic(window)) ShowWindow(window, SwRestore);
                SetForegroundWindow(window);
                return;
            }
            Thread.Sleep(100);
        }
    }
}

internal sealed record AppPaths(
    string AppRoot,
    string HarnessRoot,
    string DataRoot,
    string ConsoleExe,
    string AcceptedEel)
{
    public static AppPaths Resolve()
    {
        var appRoot = Path.GetFullPath(AppContext.BaseDirectory);
        var explicitHarness = Environment.GetEnvironmentVariable("AXIOM_HARNESS_ROOT");
        var harnessRoot = !string.IsNullOrWhiteSpace(explicitHarness)
            ? Path.GetFullPath(explicitHarness)
            : FindHarnessRoot(appRoot) ?? appRoot;
        var repositoryRoot = Directory.GetParent(harnessRoot)?.FullName ?? harnessRoot;
        var developmentLayout = File.Exists(Path.Combine(harnessRoot, "AxiomJamesDSPController", "AxiomJamesDSPController.csproj"));
        var explicitData = Environment.GetEnvironmentVariable("AXIOM_DATA_ROOT");
        var dataRoot = !string.IsNullOrWhiteSpace(explicitData)
            ? Path.GetFullPath(explicitData)
            : developmentLayout
                ? harnessRoot
                : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Axiom", "JamesDSPController");

        var consoleExe = FirstExistingFile(
            Environment.GetEnvironmentVariable("AXIOM_CONSOLE_EXE"),
            Path.Combine(appRoot, "AxiomJamesDSPConsole.exe"),
            Path.Combine(appRoot, "processor", "AxiomJamesDSPConsole.exe"),
            Path.Combine(repositoryRoot, "build-axiom-console", "AxiomJamesDSPConsole.exe"))
            ?? Path.Combine(appRoot, "AxiomJamesDSPConsole.exe");

        var acceptedEel = FirstExistingFile(
            Environment.GetEnvironmentVariable("AXIOM_ACCEPTED_EEL"),
            Path.Combine(appRoot, "assets", "Liveprog", "axiom_binaural_dsp_v4.1.4.11.eel"),
            Path.Combine(repositoryRoot, "JamesDSP-Windows", "build-final", "assets", "Liveprog", "axiom_binaural_dsp_v4.1.4.11.eel"),
            Path.Combine(harnessRoot, "runtime", "axiom-liveprog-current.eel"))
            ?? Path.Combine(appRoot, "assets", "Liveprog", "axiom_binaural_dsp_v4.1.4.11.eel");

        return new AppPaths(appRoot, harnessRoot, dataRoot, consoleExe, acceptedEel);
    }

    private static string? FindHarnessRoot(string start)
    {
        var current = new DirectoryInfo(start);
        while (current is not null)
        {
            if (current.Name.Equals("AxiomConsoleHarness", StringComparison.OrdinalIgnoreCase)
                || File.Exists(Path.Combine(current.FullName, "axiom-liveprog-test.ini")))
            {
                return current.FullName;
            }
            current = current.Parent;
        }
        return null;
    }

    private static string? FirstExistingFile(params string?[] candidates)
    {
        return candidates.FirstOrDefault(candidate => !string.IsNullOrWhiteSpace(candidate) && File.Exists(candidate));
    }
}

internal sealed record DeviceInfo(int Index, string Name, string Id);
internal sealed record ProfileListItem(string Path, string Name, string Type)
{
    public override string ToString() => Type.Equals("qualification", StringComparison.OrdinalIgnoreCase)
        ? Name + " (Protected)"
        : Name;
}
internal sealed record HealthSample(
    DateTime TimestampUtc,
    long Frames,
    long Dropped,
    long Silent,
    long Packets,
    long ConversionErrors,
    long Discontinuities,
    long RenderStarvations,
    long RenderErrors,
    long DspAverageUs,
    long DspMaximumUs,
    long DspCalls,
    long DspDeadlineMisses,
    long DspCriticalStalls,
    long PaddingMinimum,
    long PaddingMaximum,
    long BufferFrames,
    string CaptureId,
    string OutputId,
    int BufferMs,
    string Profile);
internal sealed record AxiomParam(string Var, string Name, decimal Default, decimal Min, decimal Max, decimal Step);
internal sealed class ControllerState
{
    public int CaptureIndex { get; set; } = -1;
    public int OutputIndex { get; set; } = -1;
    public string CaptureId { get; set; } = "";
    public string OutputId { get; set; } = "";
    public int BufferMs { get; set; } = 200;
    public bool SetupCompleted { get; set; } = false;
    public string PreviousDefaultOutputId { get; set; } = "";
    public bool OwnsWindowsDefault { get; set; } = false;
    public bool RestoreDefaultOnExit { get; set; } = true;
    public bool CloseToTray { get; set; } = false;
    public bool StartWithWindows { get; set; } = false;
    public bool AutoStartProcessor { get; set; } = false;
    public bool AutoFollowListeningDevice { get; set; } = true;
}

internal sealed class AxiomProfile
{
    public int SchemaVersion { get; set; } = 2;
    public string Name { get; set; } = "";
    public string Type { get; set; } = "";
    public DateTime SavedAtUtc { get; set; } = DateTime.UtcNow;
    public int CaptureIndex { get; set; } = -1;
    public int OutputIndex { get; set; } = -1;
    public string CaptureId { get; set; } = "";
    public string OutputId { get; set; } = "";
    public int BufferMs { get; set; } = 200;
    public string LiveProgFile { get; set; } = "";
    public bool LiveProgEnabled { get; set; } = true;
    public decimal PostGain { get; set; } = 0;
    public bool CrossfeedEnabled { get; set; } = false;
    public int CrossfeedMode { get; set; } = 0;
    public Dictionary<string, Dictionary<string, string>> Config { get; set; } = new(StringComparer.OrdinalIgnoreCase);
    public Dictionary<string, decimal> AxiomValues { get; set; } = new(StringComparer.OrdinalIgnoreCase);
}

internal sealed class MainForm : Form
{
    private readonly AppPaths paths;
    private readonly string configPath;
    private readonly string sourceEelPath;
    private readonly string runtimeDir;
    private readonly string profileDir;
    private readonly string diagnosticsDir;
    private readonly string healthHistoryPath;
    private readonly string runtimeEelPath;
    private readonly string testLowCutEelPath;
    private readonly string testPulseGateEelPath;
    private readonly string consoleExe;
    private readonly string statePath;
    private readonly Dictionary<string, Dictionary<string, string>> config = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, decimal> axiomValues = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, NumericUpDown> numericControls = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, NumericUpDown> axiomControls = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, CheckBox> checkControls = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, ComboBox> comboControls = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, TextBox> textControls = new(StringComparer.OrdinalIgnoreCase);
    private readonly List<AxiomParam> axiomParams = new();
    private readonly System.Windows.Forms.Timer saveTimer = new() { Interval = 250 };
    private readonly System.Windows.Forms.Timer statusTimer = new() { Interval = 1000 };
    private readonly System.Windows.Forms.Timer routeMonitorTimer = new() { Interval = 3000 };
    private readonly System.Windows.Forms.Timer processorRestartTimer = new() { Interval = 1500 };
    private ControllerState controllerState = new();
    private DateTime processorStartedAt = DateTime.MinValue;
    private readonly DateTime sessionStartedAt = DateTime.Now;
    private long lastAudioPackets = 0;
    private bool suppressUiEvents = false;
    private bool routeRecoveryPending = false;
    private bool restartAfterRouteRecovery = false;
    private bool appClosing = false;
    private bool exitRequested = false;
    private bool processorStopRequested = false;
    private bool processorRestartPending = false;
    private int unexpectedExitCount = 0;
    private DateTime unexpectedExitWindowStarted = DateTime.MinValue;
    private string processorFailureState = "";
    private bool profileDirty = false;
    private string lastDeviceFingerprint = "";
    private string lastRouteEvent = "No route events yet.";

    private readonly Icon appIcon = LoadAppIcon();
    private Process? processor;
    private NotifyIcon trayIcon = new();
    private ComboBox captureDevice = new();
    private ComboBox outputDevice = new();
    private ComboBox latencyMode = new();
    private ComboBox profileSelector = new();
    private TextBox profileNameText = new();
    private TabControl mainTabs = new();
    private TabPage setupTab = new();
    private Label statusLabel = new();
    private Label setupFilesLabel = new();
    private Label setupCableLabel = new();
    private Label setupRouteLabel = new();
    private Label setupProfileLabel = new();
    private Label setupReadyLabel = new();
    private Label captureFormatLabel = new();
    private Label renderFormatLabel = new();
    private Label bufferLabel = new();
    private Label audioHealthLabel = new();
    private Label performanceHealthLabel = new();
    private Label routeHelpLabel = new();
    private Label routeStateLabel = new();
    private Label routeStatusLabel = new();
    private Label routeEventLabel = new();
    private Label windowsDefaultLabel = new();
    private Label scriptStatusLabel = new();
    private Label diagnosticsRouteStateLabel = new();
    private Label diagnosticsRouteLabel = new();
    private Label diagnosticsRouteEventLabel = new();
    private Label diagnosticsScriptLabel = new();
    private Label diagnosticsProcessorLabel = new();
    private Label diagnosticsProfileLabel = new();
    private Label profileManagerStatusLabel = new();
    private TextBox logBox = new();
    private TextBox diagnosticsBox = new();
    private TextBox? liveProgFileText;
    private Button startButton = new();
    private Button stopButton = new();
    private string lastProcessorCommand = "";
    private string lastProfileName = "Manual";
    private string lastCaptureFormat = "--";
    private string lastRenderFormat = "--";
    private string lastBufferStatus = "--";
    private string lastAudioHealth = "Frames: -- | Dropped: -- | Silent: -- | Packets: -- | Conversion errors: --";
    private string lastPerformanceHealth = "Discontinuities: -- | Render starvations: -- | Render errors: -- | DSP avg/max: -- | Deadline misses: -- | Critical stalls: -- | Padding: --";
    private long previousDropped = 0;
    private long previousConversionErrors = 0;
    private long previousDiscontinuities = 0;
    private long previousRenderStarvations = 0;
    private long previousRenderErrors = 0;
    private long previousDspCalls = 0;
    private long previousDspDeadlineMisses = 0;
    private long previousDspCriticalStalls = 0;
    private bool healthCounterBaselineEstablished = false;
    private int sessionWarningCount = 0;

    public MainForm()
    {
        paths = AppPaths.Resolve();
        Directory.CreateDirectory(paths.DataRoot);
        configPath = Path.Combine(paths.DataRoot, "axiom-liveprog-test.ini");
        sourceEelPath = paths.AcceptedEel;
        runtimeDir = Path.Combine(paths.DataRoot, "runtime");
        profileDir = Path.Combine(paths.DataRoot, "profiles");
        diagnosticsDir = Path.Combine(paths.DataRoot, "diagnostics");
        healthHistoryPath = Path.Combine(diagnosticsDir, "health-history.jsonl");
        runtimeEelPath = Path.Combine(runtimeDir, "axiom-liveprog-current.eel");
        testLowCutEelPath = Path.Combine(runtimeDir, "axiom-test-lowcut.eel");
        testPulseGateEelPath = Path.Combine(runtimeDir, "axiom-test-pulse-gate.eel");
        consoleExe = paths.ConsoleExe;
        statePath = Path.Combine(paths.DataRoot, "controller-state.json");

        SeedDataFiles();

        Text = "Axiom JamesDSP Controller";
        Icon = appIcon;
        Width = 1180;
        Height = 760;
        MinimumSize = new Size(980, 620);
        StartPosition = FormStartPosition.CenterScreen;
        BackColor = Color.FromArgb(18, 19, 22);
        ForeColor = Color.FromArgb(235, 238, 242);

        saveTimer.Tick += (_, _) =>
        {
            saveTimer.Stop();
            SaveConfigAndRuntimeEel();
        };
        statusTimer.Tick += (_, _) => UpdateStatus();
        routeMonitorTimer.Tick += (_, _) => MonitorRouteDevices();
        processorRestartTimer.Tick += (_, _) => RestartProcessorAfterFailure();

        LoadConfig();
        NormalizePortableConfigPaths();
        LoadControllerState();
        LoadAxiomParams();
        LoadRuntimeAxiomValues();
        BuildUi();
        InitializeTrayIcon();
        RefreshDevices();
        RunFirstRunChecks();
        SaveConfigAndRuntimeEel(selectAxiomRuntime: string.IsNullOrWhiteSpace(GetValue("LiveProg", "file", "")));
        statusTimer.Start();
        routeMonitorTimer.Start();
        UpdateStatus();
        if (controllerState.AutoStartProcessor)
        {
            BeginInvoke(() =>
            {
                if (ValidateRoute(captureDevice.SelectedItem as DeviceInfo, outputDevice.SelectedItem as DeviceInfo) is null)
                {
                    StartProcessor();
                }
            });
        }
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        if (!exitRequested && controllerState.CloseToTray && e.CloseReason == CloseReason.UserClosing)
        {
            e.Cancel = true;
            Hide();
            trayIcon.Visible = true;
            return;
        }
        appClosing = true;
        saveTimer.Stop();
        statusTimer.Stop();
        routeMonitorTimer.Stop();
        processorRestartTimer.Stop();
        WriteSessionSummary();
        StopAllProcessors();
        if (controllerState.RestoreDefaultOnExit) RestorePreviousWindowsDefault();
        trayIcon.Visible = false;
        trayIcon.Dispose();
        base.OnFormClosing(e);
    }

    protected override void OnResize(EventArgs e)
    {
        base.OnResize(e);
        if (controllerState.CloseToTray && WindowState == FormWindowState.Minimized)
        {
            Hide();
            trayIcon.Visible = true;
        }
    }

    private void BuildUi()
    {
        mainTabs = new TabControl { Dock = DockStyle.Fill };
        setupTab = BuildSetupTab();
        mainTabs.TabPages.Add(BuildRoutingTab());
        mainTabs.TabPages.Add(BuildAxiomTab());
        mainTabs.TabPages.Add(BuildProfilesTab());
        mainTabs.TabPages.Add(BuildCoreEffectsTab());
        mainTabs.TabPages.Add(BuildEqDynamicsTab());
        mainTabs.TabPages.Add(BuildFilesTab());
        mainTabs.TabPages.Add(BuildDiagnosticsTab());
        mainTabs.TabPages.Add(setupTab);
        Controls.Add(mainTabs);
        mainTabs.SelectedIndex = 0;
    }

    private TabPage BuildProfilesTab()
    {
        var page = NewPage("Profiles");
        var panel = NewStack();
        page.Controls.Add(panel);

        profileManagerStatusLabel = HealthLabel("Profile state: Manual");
        panel.Controls.Add(profileManagerStatusLabel);

        var selectRow = Row();
        profileSelector = new ComboBox { Width = 420, DropDownStyle = ComboBoxStyle.DropDownList };
        profileSelector.SelectedIndexChanged += (_, _) =>
        {
            if (profileSelector.SelectedItem is ProfileListItem item) profileNameText.Text = item.Name;
            UpdateProfileManagerStatus();
        };
        selectRow.Controls.Add(Label("Saved profile"));
        selectRow.Controls.Add(profileSelector);
        selectRow.Controls.Add(Button("Refresh", (_, _) => RefreshProfileList()));
        panel.Controls.Add(selectRow);

        var nameRow = Row();
        profileNameText = new TextBox { Width = 420, Text = "My Listening Profile" };
        nameRow.Controls.Add(Label("Profile name"));
        nameRow.Controls.Add(profileNameText);
        panel.Controls.Add(nameRow);

        var primary = Row();
        primary.Controls.Add(Label("Profile actions"));
        primary.Controls.Add(Button("Save New", (_, _) => SaveNamedProfile()));
        primary.Controls.Add(Button("Update Selected", (_, _) => UpdateSelectedProfile()));
        primary.Controls.Add(Button("Load Selected", (_, _) => LoadSelectedProfile()));
        primary.Controls.Add(Button("Duplicate", (_, _) => DuplicateSelectedProfile()));
        primary.Controls.Add(Button("Rename", (_, _) => RenameSelectedProfile()));
        primary.Controls.Add(Button("Delete", (_, _) => DeleteSelectedProfile()));
        panel.Controls.Add(primary);

        var transfer = Row();
        transfer.Controls.Add(Label("Transfer"));
        transfer.Controls.Add(Button("Import", (_, _) => ImportProfile()));
        transfer.Controls.Add(Button("Export", (_, _) => ExportSelectedProfile()));
        transfer.Controls.Add(Button("Load Qualification Baseline", (_, _) => LoadQualificationBaseline()));
        panel.Controls.Add(transfer);

        RefreshProfileList();
        return page;
    }

    private TabPage BuildSetupTab()
    {
        var page = NewPage("Setup & System");
        var panel = NewStack();
        page.Controls.Add(panel);

        setupReadyLabel = new Label
        {
            AutoSize = true,
            Font = new Font(Font.FontFamily, 11, FontStyle.Bold),
            ForeColor = Color.FromArgb(240, 170, 80),
            Text = "Setup check pending"
        };
        panel.Controls.Add(setupReadyLabel);

        setupFilesLabel = HealthLabel("Application files: --");
        setupCableLabel = HealthLabel("VB-CABLE: --");
        setupRouteLabel = HealthLabel("Audio route: --");
        setupProfileLabel = HealthLabel("Listening profile: --");
        panel.Controls.Add(setupFilesLabel);
        panel.Controls.Add(setupCableLabel);
        panel.Controls.Add(setupRouteLabel);
        panel.Controls.Add(setupProfileLabel);

        var actions = Row();
        actions.Controls.Add(Button("Refresh Setup Check", (_, _) => RefreshSetupStatus()));
        actions.Controls.Add(Button("Apply Recommended Route", (_, _) => ApplyRecommendedSetup()));
        actions.Controls.Add(Button("Run Pulse Test", (_, _) => LoadPulseGateTest()));
        actions.Controls.Add(Button("Save Listening Profile", (_, _) => SaveProfile("listening")));
        actions.Controls.Add(Button("Complete Setup", (_, _) => CompleteSetup()));
        panel.Controls.Add(actions);

        var folders = Row();
        folders.Controls.Add(Button("Create Desktop Shortcut", (_, _) => CreateDesktopShortcut()));
        folders.Controls.Add(Button("Open Data Folder", (_, _) => OpenFolder(paths.DataRoot)));
        folders.Controls.Add(Button("Open Application Folder", (_, _) => OpenFolder(paths.AppRoot)));
        panel.Controls.Add(folders);

        var lifecycle = Row();
        lifecycle.Controls.Add(Label("Application lifecycle"));
        lifecycle.Controls.Add(StateCheckBox("Close to notification area", controllerState.CloseToTray, value =>
        {
            controllerState.CloseToTray = value;
            SaveControllerState();
        }));
        lifecycle.Controls.Add(StateCheckBox("Start with Windows", controllerState.StartWithWindows, value =>
        {
            controllerState.StartWithWindows = value;
            ConfigureWindowsStartup(value);
            SaveControllerState();
        }));
        lifecycle.Controls.Add(StateCheckBox("Start processor automatically", controllerState.AutoStartProcessor, value =>
        {
            controllerState.AutoStartProcessor = value;
            SaveControllerState();
        }));
        lifecycle.Controls.Add(StateCheckBox("Follow listening device", controllerState.AutoFollowListeningDevice, value =>
        {
            controllerState.AutoFollowListeningDevice = value;
            SaveControllerState();
            if (value) AutoRouteListeningDevice(restartProcessor: IsProcessorRunning());
        }));
        lifecycle.Controls.Add(StateCheckBox("Restore previous output on exit", controllerState.RestoreDefaultOnExit, value =>
        {
            controllerState.RestoreDefaultOnExit = value;
            SaveControllerState();
        }));
        panel.Controls.Add(lifecycle);
        return page;
    }

    private TabPage BuildRoutingTab()
    {
        var page = NewPage("Routing");
        var panel = NewStack();
        page.Controls.Add(panel);

        statusLabel = new Label { AutoSize = true, Font = new Font(Font.FontFamily, 11, FontStyle.Bold), Text = "Stopped" };
        panel.Controls.Add(statusLabel);

        var healthGroup = new GroupBox
        {
            Text = "Audio Health",
            ForeColor = ForeColor,
            AutoSize = true,
            Dock = DockStyle.Top,
            Padding = new Padding(10)
        };
        var healthStack = NewStack();
        healthStack.Dock = DockStyle.Top;
        healthStack.AutoSize = true;
        healthStack.AutoScroll = false;
        healthStack.Padding = new Padding(0);
        captureFormatLabel = HealthLabel("Capture: --");
        renderFormatLabel = HealthLabel("Render: --");
        bufferLabel = HealthLabel("Buffer: --");
        audioHealthLabel = HealthLabel("Frames: -- | Dropped: -- | Silent: -- | Packets: -- | Conversion errors: --");
        performanceHealthLabel = HealthLabel("Discontinuities: -- | Render starvations: -- | Render errors: -- | DSP avg/max: -- | Deadline misses: -- | Critical stalls: -- | Padding: --");
        healthStack.Controls.Add(captureFormatLabel);
        healthStack.Controls.Add(renderFormatLabel);
        healthStack.Controls.Add(bufferLabel);
        healthStack.Controls.Add(audioHealthLabel);
        healthStack.Controls.Add(performanceHealthLabel);
        healthGroup.Controls.Add(healthStack);
        panel.Controls.Add(healthGroup);

        routeHelpLabel = new Label
        {
            AutoSize = true,
            MaximumSize = new Size(1050, 0),
            ForeColor = Color.FromArgb(210, 205, 170),
            Padding = new Padding(4),
            Text = "Route the player/browser into the capture/source endpoint, then send processed audio to a different real output endpoint. If the processor is stopped, control changes are only saved."
        };
        panel.Controls.Add(routeHelpLabel);

        routeStateLabel = HealthLabel("Route state: --");
        routeStateLabel.MaximumSize = new Size(1050, 0);
        panel.Controls.Add(routeStateLabel);

        routeStatusLabel = HealthLabel("Route: --");
        routeStatusLabel.MaximumSize = new Size(1050, 0);
        panel.Controls.Add(routeStatusLabel);

        routeEventLabel = HealthLabel("Last route event: --");
        routeEventLabel.MaximumSize = new Size(1050, 0);
        panel.Controls.Add(routeEventLabel);

        scriptStatusLabel = HealthLabel("Script: --");
        scriptStatusLabel.MaximumSize = new Size(1050, 0);
        panel.Controls.Add(scriptStatusLabel);

        windowsDefaultLabel = HealthLabel("Windows default: --");
        windowsDefaultLabel.MaximumSize = new Size(1050, 0);
        panel.Controls.Add(windowsDefaultLabel);

        var sourceRow = Row();
        captureDevice = new ComboBox { Width = 520, DropDownStyle = ComboBoxStyle.DropDownList };
        captureDevice.SelectedIndexChanged += (_, _) => { if (!suppressUiEvents) SaveSelectedRoute(); };
        sourceRow.Controls.Add(Label("Capture/source endpoint"));
        sourceRow.Controls.Add(captureDevice);
        panel.Controls.Add(sourceRow);

        var row = Row();
        outputDevice = new ComboBox { Width = 520, DropDownStyle = ComboBoxStyle.DropDownList };
        outputDevice.SelectedIndexChanged += (_, _) => { if (!suppressUiEvents) SaveSelectedRoute(); };
        var refreshButton = Button("Refresh Devices", (_, _) => RefreshDevices());
        row.Controls.Add(Label("Processed output endpoint"));
        row.Controls.Add(outputDevice);
        row.Controls.Add(refreshButton);
        panel.Controls.Add(row);

        var latencyRow = Row();
        latencyMode = new ComboBox { Width = 260, DropDownStyle = ComboBoxStyle.DropDownList };
        latencyMode.Items.AddRange(new object[]
        {
            "Low latency - 30 ms",
            "Balanced - 60 ms",
            "Safe playback - 100 ms",
            "Extra safe - 150 ms",
            "Resilient - 200 ms"
        });
        latencyMode.SelectedIndexChanged += (_, _) =>
        {
            if (suppressUiEvents) return;
            controllerState.BufferMs = SelectedBufferMs();
            profileDirty = true;
            SaveControllerState();
            bufferLabel.Text = $"Buffer: {controllerState.BufferMs} ms selected";
            if (IsProcessorRunning()) AppendLog("Latency mode changed. Restart processor to apply the new buffer target.");
        };
        latencyRow.Controls.Add(Label("Latency mode"));
        latencyRow.Controls.Add(latencyMode);
        panel.Controls.Add(latencyRow);

        var buttons = Row();
        startButton = Button("Start Processor", (_, _) => StartProcessor());
        stopButton = Button("Stop Processor", (_, _) => StopProcessor());
        buttons.Controls.Add(startButton);
        buttons.Controls.Add(stopButton);
        buttons.Controls.Add(Button("Stop All Processors", (_, _) => StopAllProcessors()));
        panel.Controls.Add(buttons);

        var routeButtons = Row();
        routeButtons.Controls.Add(Label("Route setup"));
        routeButtons.Controls.Add(Button("Use VB-CABLE -> Output", (_, _) => UseVbCableToSelectedOutputRoute()));
        routeButtons.Controls.Add(Button("Use Steam -> Realtek", (_, _) => UseSteamToRealtekRoute()));
        routeButtons.Controls.Add(Button("Use Steam -> EarPods", (_, _) => UseSteamToEarPodsRoute()));
        routeButtons.Controls.Add(Button("Set Windows Source Default", (_, _) => SetWindowsDefaultToCapture()));
        routeButtons.Controls.Add(Button("Restore Previous Default", (_, _) => RestorePreviousWindowsDefault()));
        panel.Controls.Add(routeButtons);

        var liveProgButtons = Row();
        liveProgButtons.Controls.Add(Label("LiveProg script"));
        liveProgButtons.Controls.Add(Button("Load Low-Cut Test", (_, _) => LoadLowCutTest()));
        liveProgButtons.Controls.Add(Button("Load Pulse Test", (_, _) => LoadPulseGateTest()));
        liveProgButtons.Controls.Add(Button("Restore Axiom EEL", (_, _) => RestoreAxiomLiveProg()));
        liveProgButtons.Controls.Add(Button("Restore Axiom + Start", (_, _) => RestoreAxiomAndStart()));
        panel.Controls.Add(liveProgButtons);

        var profileButtons = Row();
        profileButtons.Controls.Add(Label("Profiles"));
        profileButtons.Controls.Add(Button("Save Listening Profile", (_, _) => SaveProfile("listening")));
        profileButtons.Controls.Add(Button("Load Listening Profile", (_, _) => LoadProfile("listening")));
        profileButtons.Controls.Add(Button("Load Qualification Baseline", (_, _) => LoadQualificationBaseline()));
        profileButtons.Controls.Add(Button("Export Diagnostic Report", (_, _) => ExportDiagnosticReport()));
        panel.Controls.Add(profileButtons);

        panel.Controls.Add(NumberControl("General", "postGain", "Master post gain (dB)", -30, 12, 0.5m, 0));
        panel.Controls.Add(CheckControl("LiveProg", "enabled", "Enable Axiom LiveProg", true));

        logBox = new TextBox
        {
            Multiline = true,
            ReadOnly = true,
            ScrollBars = ScrollBars.Vertical,
            Height = 430,
            Dock = DockStyle.Top,
            BackColor = Color.FromArgb(8, 10, 12),
            ForeColor = Color.FromArgb(210, 225, 215),
            Font = new Font("Consolas", 9)
        };
        panel.Controls.Add(logBox);
        return page;
    }

    private TabPage BuildAxiomTab()
    {
        var page = NewPage("Axiom");
        var panel = NewStack();
        page.Controls.Add(panel);
        panel.Controls.Add(Label("Axiom Clean R011 LiveProg controls. Changes write a runtime EEL copy and auto-reload the processor."));

        foreach (var param in axiomParams)
        {
            panel.Controls.Add(AxiomNumberControl(param));
        }
        return page;
    }

    private TabPage BuildCoreEffectsTab()
    {
        var page = NewPage("Core Effects");
        var panel = NewStack();
        page.Controls.Add(panel);

        panel.Controls.Add(CheckControl("BassBoost", "enabled", "Bass boost", false));
        panel.Controls.Add(NumberControl("BassBoost", "gain", "Bass boost gain (dB)", 0, 15, 0.5m, 0));
        panel.Controls.Add(CheckControl("StereoWide", "enabled", "Stereo wide", false));
        panel.Controls.Add(NumberControl("StereoWide", "level", "Stereo wide level (%)", 0, 100, 1, 0));
        panel.Controls.Add(CheckControl("Reverb", "enabled", "Reverb", false));
        panel.Controls.Add(ComboControl("Reverb", "preset", "Reverb preset", new[] { "0 Default", "1 SmallHall1", "2 SmallHall2", "3 MediumHall1", "4 MediumHall2", "5 LargeHall1", "6 LargeHall2", "7 SmallRoom1", "8 SmallRoom2", "9 MediumRoom1", "10 MediumRoom2", "11 LargeRoom1", "12 LargeRoom2" }, 0));
        panel.Controls.Add(CheckControl("Tube", "enabled", "Vacuum tube", false));
        panel.Controls.Add(NumberControl("Tube", "gain", "Tube gain (dB)", 0, 24, 0.5m, 0));
        panel.Controls.Add(CheckControl("Crossfeed", "enabled", "Crossfeed", false));
        panel.Controls.Add(ComboControl("Crossfeed", "mode", "Crossfeed mode", new[] { "0 BS2B Lv1", "1 BS2B Lv2", "2 HRTF Crossfeed", "3 HRTF Surround1", "4 HRTF Surround2", "5 HRTF Surround3" }, 0));
        return page;
    }

    private TabPage BuildEqDynamicsTab()
    {
        var page = NewPage("EQ + Dynamics");
        var panel = NewStack();
        page.Controls.Add(panel);

        panel.Controls.Add(CheckControl("Compressor", "enabled", "Compressor", false));
        panel.Controls.Add(NumberControl("Compressor", "granularity", "Compressor granularity", 1, 3, 1, 1));
        panel.Controls.Add(NumberControl("Compressor", "timeConstant", "Compressor time constant", 0, 1, 0.01m, 0.56m));
        panel.Controls.Add(ComboControl("Compressor", "resolution", "Compressor resolution", new[] { "0 Low", "1 Medium", "2 High" }, 0));
        panel.Controls.Add(ArrayControl("Compressor", "gains", "Compressor band gains", new[] { "95", "200", "400", "800", "1600", "3400", "7500" }, -24, 24, 0.5m));

        panel.Controls.Add(CheckControl("Equalizer", "enabled", "15-band equalizer", false));
        panel.Controls.Add(ComboControl("Equalizer", "mode", "EQ mode", new[] { "0 FIR linear phase", "1 IIR minimum phase" }, 0));
        panel.Controls.Add(ComboControl("Equalizer", "interpolation", "EQ interpolation", new[] { "0 Linear", "1 Cubic spline" }, 0));
        panel.Controls.Add(ArrayControl("Equalizer", "gains", "EQ gains", new[] { "25", "40", "63", "100", "160", "250", "400", "630", "1k", "1.6k", "2.5k", "4k", "6.3k", "10k", "16k" }, -24, 24, 0.5m));
        return page;
    }

    private TabPage BuildFilesTab()
    {
        var page = NewPage("Files");
        var panel = NewStack();
        page.Controls.Add(panel);
        panel.Controls.Add(CheckControl("DDC", "enabled", "Enable DDC", false));
        panel.Controls.Add(FileControl("DDC", "file", "DDC file", "VDC files (*.vdc)|*.vdc|All files (*.*)|*.*"));
        panel.Controls.Add(CheckControl("Convolver", "enabled", "Enable convolver", false));
        panel.Controls.Add(FileControl("Convolver", "file", "Impulse response", "Audio files (*.wav;*.flac)|*.wav;*.flac|All files (*.*)|*.*"));
        panel.Controls.Add(FileControl("LiveProg", "file", "LiveProg source", "EEL files (*.eel)|*.eel|All files (*.*)|*.*"));
        return page;
    }

    private TabPage BuildDiagnosticsTab()
    {
        var page = NewPage("Diagnostics");
        var panel = NewStack();
        page.Controls.Add(panel);

        diagnosticsRouteStateLabel = HealthLabel("Route state: --");
        diagnosticsRouteStateLabel.MaximumSize = new Size(1050, 0);
        diagnosticsRouteLabel = HealthLabel("Route: --");
        diagnosticsRouteLabel.MaximumSize = new Size(1050, 0);
        diagnosticsRouteEventLabel = HealthLabel("Last route event: --");
        diagnosticsRouteEventLabel.MaximumSize = new Size(1050, 0);
        diagnosticsScriptLabel = HealthLabel("Script: --");
        diagnosticsScriptLabel.MaximumSize = new Size(1050, 0);
        diagnosticsProcessorLabel = HealthLabel("Processor: --");
        diagnosticsProcessorLabel.MaximumSize = new Size(1050, 0);
        diagnosticsProfileLabel = HealthLabel("Profile: Manual");
        diagnosticsProfileLabel.MaximumSize = new Size(1050, 0);

        panel.Controls.Add(diagnosticsRouteStateLabel);
        panel.Controls.Add(diagnosticsRouteLabel);
        panel.Controls.Add(diagnosticsRouteEventLabel);
        panel.Controls.Add(diagnosticsScriptLabel);
        panel.Controls.Add(diagnosticsProcessorLabel);
        panel.Controls.Add(diagnosticsProfileLabel);
        var diagnosticActions = Row();
        diagnosticActions.Controls.Add(Button("Export Diagnostic Report", (_, _) => ExportDiagnosticReport()));
        diagnosticActions.Controls.Add(Button("Copy Diagnostic Summary", (_, _) => CopyDiagnosticSummary()));
        diagnosticActions.Controls.Add(Button("Open Health History", (_, _) => OpenHealthHistory()));
        diagnosticActions.Controls.Add(Button("Clear Health History", (_, _) => ClearHealthHistory()));
        panel.Controls.Add(diagnosticActions);

        diagnosticsBox = new TextBox
        {
            Multiline = true,
            ReadOnly = true,
            ScrollBars = ScrollBars.Vertical,
            Height = 520,
            Dock = DockStyle.Top,
            BackColor = Color.FromArgb(8, 10, 12),
            ForeColor = Color.FromArgb(210, 225, 215),
            Font = new Font("Consolas", 9),
            Text = "Diagnostics appear here after processor output is received or a report is exported."
        };
        panel.Controls.Add(diagnosticsBox);
        return page;
    }

    private Control AxiomNumberControl(AxiomParam param)
    {
        var box = NumberRow(param.Name, param.Min, param.Max, param.Step, axiomValues.GetValueOrDefault(param.Var, param.Default));
        var input = (NumericUpDown)box.Tag!;
        axiomControls[param.Var] = input;
        input.ValueChanged += (_, _) =>
        {
            if (suppressUiEvents) return;
            axiomValues[param.Var] = input.Value;
            QueueSave();
        };
        return box;
    }

    private Control NumberControl(string section, string key, string label, decimal min, decimal max, decimal step, decimal fallback)
    {
        var box = NumberRow(label, min, max, step, GetDecimal(section, key, fallback));
        var input = (NumericUpDown)box.Tag!;
        numericControls[ConfigKey(section, key)] = input;
        input.ValueChanged += (_, _) =>
        {
            if (suppressUiEvents) return;
            SetValue(section, key, input.Value.ToString(CultureInfo.InvariantCulture));
            QueueSave();
        };
        return box;
    }

    private Control CheckControl(string section, string key, string label, bool fallback)
    {
        var check = new CheckBox
        {
            Text = label,
            Checked = GetBool(section, key, fallback),
            AutoSize = true,
            ForeColor = ForeColor,
            Padding = new Padding(4)
        };
        checkControls[ConfigKey(section, key)] = check;
        check.CheckedChanged += (_, _) =>
        {
            if (suppressUiEvents) return;
            SetValue(section, key, check.Checked ? "true" : "false");
            QueueSave();
        };
        return check;
    }

    private Control ComboControl(string section, string key, string label, string[] items, int fallback)
    {
        var row = Row();
        var combo = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 260 };
        combo.Items.AddRange(items);
        combo.SelectedIndex = Math.Clamp(GetInt(section, key, fallback), 0, items.Length - 1);
        comboControls[ConfigKey(section, key)] = combo;
        combo.SelectedIndexChanged += (_, _) =>
        {
            if (suppressUiEvents) return;
            SetValue(section, key, combo.SelectedIndex.ToString(CultureInfo.InvariantCulture));
            QueueSave();
        };
        row.Controls.Add(Label(label));
        row.Controls.Add(combo);
        return row;
    }

    private Control ArrayControl(string section, string key, string label, string[] names, decimal min, decimal max, decimal step)
    {
        var group = new GroupBox { Text = label, ForeColor = ForeColor, AutoSize = true, Dock = DockStyle.Top, Padding = new Padding(10) };
        var flow = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, WrapContents = true };
        group.Controls.Add(flow);
        var values = GetArray(section, key, names.Length);
        var inputs = new List<NumericUpDown>();
        for (int i = 0; i < names.Length; i++)
        {
            var item = NumberRow(names[i], min, max, step, values[i], 92);
            inputs.Add((NumericUpDown)item.Tag!);
            flow.Controls.Add(item);
        }
        foreach (var input in inputs)
        {
            input.ValueChanged += (_, _) =>
            {
                if (suppressUiEvents) return;
                SetValue(section, key, string.Join(",", inputs.Select(i => i.Value.ToString(CultureInfo.InvariantCulture))));
                QueueSave();
            };
        }
        return group;
    }

    private Control FileControl(string section, string key, string label, string filter)
    {
        var row = Row();
        var text = new TextBox { Width = 620, Text = GetValue(section, key, "") };
        if (section.Equals("LiveProg", StringComparison.OrdinalIgnoreCase) && key.Equals("file", StringComparison.OrdinalIgnoreCase))
        {
            liveProgFileText = text;
        }
        textControls[ConfigKey(section, key)] = text;
        text.TextChanged += (_, _) =>
        {
            if (suppressUiEvents) return;
            SetValue(section, key, text.Text.Trim());
            QueueSave();
        };
        var browse = Button("Browse", (_, _) =>
        {
            using var dialog = new OpenFileDialog { Filter = filter };
            if (File.Exists(text.Text)) dialog.InitialDirectory = Path.GetDirectoryName(text.Text);
            if (dialog.ShowDialog(this) == DialogResult.OK) text.Text = dialog.FileName;
        });
        row.Controls.Add(Label(label));
        row.Controls.Add(text);
        row.Controls.Add(browse);
        return row;
    }

    private Panel NumberRow(string label, decimal min, decimal max, decimal step, decimal value, int width = 130)
    {
        var row = Row();
        var input = new NumericUpDown
        {
            Minimum = min,
            Maximum = max,
            Increment = step,
            DecimalPlaces = DecimalPlaces(step),
            Value = Math.Clamp(value, min, max),
            Width = width
        };
        row.Controls.Add(Label(label));
        row.Controls.Add(input);
        row.Tag = input;
        return row;
    }

    private void StartProcessor(bool automaticRestart = false)
    {
        if (!automaticRestart)
        {
            unexpectedExitCount = 0;
            unexpectedExitWindowStarted = DateTime.MinValue;
            processorFailureState = "";
            processorRestartPending = false;
            processorRestartTimer.Stop();
        }
        if (controllerState.AutoFollowListeningDevice)
        {
            AutoRouteListeningDevice(restartProcessor: false);
        }
        StopAllProcessors();
        SaveConfigAndRuntimeEel();
        var capture = captureDevice.SelectedItem as DeviceInfo;
        var output = outputDevice.SelectedItem as DeviceInfo;
        var routeError = ValidateRoute(capture, output);
        if (routeError is not null)
        {
            AppendLog("Start blocked: " + routeError);
            UpdateStatus();
            return;
        }

        var scriptError = ValidateLiveProgScript(GetValue("LiveProg", "file", ""));
        if (scriptError is not null)
        {
            AppendLog("Start blocked: " + scriptError);
            UpdateStatus();
            return;
        }

        if (capture is not null && output is not null && capture.Id == output.Id)
        {
            AppendLog("Start blocked: capture/source endpoint and processed output endpoint must be different.");
            UpdateStatus();
            return;
        }

        var args = new StringBuilder();
        args.Append("--watch-config ");
        args.Append("-c ").Append(Quote(configPath)).Append(' ');
        if (capture is not null) args.Append("-i ").Append(capture.Index).Append(' ');
        if (output is not null) args.Append("-o ").Append(output.Index);
        args.Append(' ').Append("--buffer-ms ").Append(controllerState.BufferMs);
        lastProcessorCommand = consoleExe + " " + args;
        var nextProcessor = new Process
        {
            StartInfo = new ProcessStartInfo(consoleExe, args.ToString())
            {
                WorkingDirectory = Path.GetDirectoryName(consoleExe)!,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            },
            EnableRaisingEvents = true
        };
        nextProcessor.OutputDataReceived += (_, e) => AppendLog(e.Data);
        nextProcessor.ErrorDataReceived += (_, e) => AppendLog(e.Data);
        nextProcessor.Exited += (_, _) => BeginInvoke(() => HandleProcessorExited(nextProcessor));
        processor = nextProcessor;
        processorStopRequested = false;
        ResetHealthCounterBaseline();
        processor.Start();
        processorStartedAt = DateTime.Now;
        lastAudioPackets = 0;
        processor.BeginOutputReadLine();
        processor.BeginErrorReadLine();
        processorFailureState = automaticRestart
            ? $"Processor recovered after automatic restart {unexpectedExitCount}/3."
            : "";
        AppendLog($"{(automaticRestart ? "Processor automatically restarted" : "Processor started")}: capture/source [{capture?.Index}] {capture?.Name}; output [{output?.Index}] {output?.Name}; buffer {controllerState.BufferMs} ms.");
        UpdateStatus();
    }

    private void StopProcessor()
    {
        processorStopRequested = true;
        processorRestartPending = false;
        processorRestartTimer.Stop();
        processorFailureState = "";
        try
        {
            if (processor is { HasExited: false })
            {
                processor.Kill(true);
                processor.WaitForExit(1500);
            }
        }
        catch { }
        processor = null;
        processorStartedAt = DateTime.MinValue;
        UpdateStatus();
    }

    private void StopAllProcessors()
    {
        processorStopRequested = true;
        processorRestartPending = false;
        processorRestartTimer.Stop();
        foreach (var process in Process.GetProcessesByName("AxiomJamesDSPConsole"))
        {
            try
            {
                process.Kill(true);
                process.WaitForExit(1500);
            }
            catch (Exception exc)
            {
                AppendLog("Failed to stop processor " + process.Id + ": " + exc.Message);
            }
            finally
            {
                process.Dispose();
            }
        }
        processor = null;
        processorStartedAt = DateTime.MinValue;
        lastAudioPackets = 0;
        UpdateStatus();
    }

    private void HandleProcessorExited(Process exitedProcessor)
    {
        if (processor != exitedProcessor) return;

        int exitCode;
        try
        {
            exitCode = exitedProcessor.ExitCode;
        }
        catch
        {
            exitCode = -1;
        }

        processor = null;
        processorStartedAt = DateTime.MinValue;
        lastAudioPackets = 0;

        if (appClosing || processorStopRequested || routeRecoveryPending)
        {
            UpdateStatus();
            return;
        }

        var now = DateTime.Now;
        if (unexpectedExitWindowStarted == DateTime.MinValue
            || (now - unexpectedExitWindowStarted).TotalSeconds > 60)
        {
            unexpectedExitWindowStarted = now;
            unexpectedExitCount = 0;
        }
        unexpectedExitCount += 1;

        var routeValid = ValidateRoute(captureDevice.SelectedItem as DeviceInfo, outputDevice.SelectedItem as DeviceInfo) is null;
        var scriptValid = ValidateLiveProgScript(GetValue("LiveProg", "file", "")) is null;
        if (unexpectedExitCount <= 3 && routeValid && scriptValid)
        {
            processorRestartPending = true;
            processorFailureState = $"Processor exited unexpectedly with code {exitCode}. Restart {unexpectedExitCount}/3 pending.";
            AppendLog(processorFailureState);
            processorRestartTimer.Stop();
            processorRestartTimer.Start();
        }
        else
        {
            processorRestartPending = false;
            processorFailureState = $"Processor stopped after repeated failures. Last exit code: {exitCode}. Manual restart required.";
            AppendLog(processorFailureState);
        }
        UpdateStatus();
    }

    private void RestartProcessorAfterFailure()
    {
        processorRestartTimer.Stop();
        if (!processorRestartPending || appClosing || routeRecoveryPending) return;
        processorRestartPending = false;
        StartProcessor(automaticRestart: true);
    }

    private void UseVbCableToSelectedOutputRoute()
    {
        var selectedOutput = outputDevice.SelectedItem as DeviceInfo;
        if (!SelectDeviceByExactName(captureDevice, "CABLE Input (VB-Audio Virtual Cable)")
            && !SelectDeviceByName(captureDevice, "CABLE Input")
            && !SelectDeviceByName(captureDevice, "VB-CABLE"))
        {
            AppendLog("VB-CABLE source endpoint was not found. Install VB-Audio Virtual Cable, then refresh devices.");
            return;
        }

        if (selectedOutput is not null)
        {
            SelectDevice(outputDevice, selectedOutput.Index, outputDevice.SelectedIndex);
        }

        if (outputDevice.SelectedItem is not DeviceInfo output
            || IsVirtualSource(output)
            || captureDevice.SelectedItem is DeviceInfo capture && output.Id == capture.Id)
        {
            SelectPreferredPhysicalOutput(outputDevice);
        }

        SaveSelectedRoute();
        SetWindowsDefaultToCapture();
        SetRouteEvent("Route preset applied: VB-CABLE source -> selected processed output.");
    }

    private void UseSteamToRealtekRoute()
    {
        SelectDeviceByName(captureDevice, "Steam Streaming Speakers");
        SelectDeviceByName(outputDevice, "Realtek");
        SaveSelectedRoute();
        SetWindowsDefaultToCapture();
        SetRouteEvent("Route preset applied: Steam Streaming Speakers -> Realtek.");
    }

    private void UseSteamToEarPodsRoute()
    {
        SelectDeviceByName(captureDevice, "Steam Streaming Speakers");
        SelectDeviceByName(outputDevice, "EarPods");
        SaveSelectedRoute();
        SetWindowsDefaultToCapture();
        SetRouteEvent("Route preset applied: Steam Streaming Speakers -> EarPods.");
    }

    private void SetWindowsDefaultToCapture()
    {
        if (captureDevice.SelectedItem is not DeviceInfo capture)
        {
            AppendLog("No capture/source endpoint selected.");
            return;
        }

        try
        {
            var currentDefault = GetWindowsDefaultDevice();
            if (!controllerState.OwnsWindowsDefault
                && currentDefault is not null
                && !currentDefault.Id.Equals(capture.Id, StringComparison.OrdinalIgnoreCase))
            {
                controllerState.PreviousDefaultOutputId = currentDefault.Id;
            }
            using var process = Process.Start(new ProcessStartInfo(consoleExe, "--set-default " + capture.Index)
            {
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            });
            var stdout = process?.StandardOutput.ReadToEnd() ?? "";
            var stderr = process?.StandardError.ReadToEnd() ?? "";
            process?.WaitForExit(3000);
            AppendLog(string.IsNullOrWhiteSpace(stdout) ? $"Windows default playback requested for [{capture.Index}] {capture.Name}." : stdout.Trim());
            if (!string.IsNullOrWhiteSpace(stderr)) AppendLog(stderr.Trim());
            if (process?.ExitCode == 0)
            {
                controllerState.OwnsWindowsDefault = true;
                SaveControllerState();
                SetRouteEvent($"Windows default playback set to source endpoint: {capture.Name}.");
                UpdateWindowsDefaultStatus();
            }
        }
        catch (Exception exc)
        {
            AppendLog("Failed to set Windows default playback: " + exc.Message);
        }
    }

    private DeviceInfo? GetWindowsDefaultDevice()
    {
        try
        {
            using var process = Process.Start(new ProcessStartInfo(consoleExe, "--get-default-json")
            {
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            });
            var json = process?.StandardOutput.ReadToEnd() ?? "";
            process?.WaitForExit(3000);
            using var doc = JsonDocument.Parse(json);
            return new DeviceInfo(
                doc.RootElement.GetProperty("index").GetInt32(),
                doc.RootElement.GetProperty("name").GetString() ?? "",
                doc.RootElement.GetProperty("id").GetString() ?? "");
        }
        catch (Exception exc)
        {
            AppendLog("Windows default endpoint check failed: " + exc.Message);
            return null;
        }
    }

    private void UpdateWindowsDefaultStatus()
    {
        var current = GetWindowsDefaultDevice();
        if (current is null)
        {
            windowsDefaultLabel.Text = "Windows default: unavailable";
            windowsDefaultLabel.ForeColor = Color.FromArgb(240, 170, 80);
            return;
        }

        var expected = captureDevice.SelectedItem as DeviceInfo;
        var matchesAxiom = expected is not null && current.Id.Equals(expected.Id, StringComparison.OrdinalIgnoreCase);
        if (controllerState.OwnsWindowsDefault && !matchesAxiom)
        {
            controllerState.OwnsWindowsDefault = false;
            SaveControllerState();
            SetRouteEvent("Windows default output changed outside Axiom; route ownership released.");
        }
        windowsDefaultLabel.Text = $"Windows default: {current.Name}; Axiom ownership: {(controllerState.OwnsWindowsDefault ? "active" : "inactive")}";
        windowsDefaultLabel.ForeColor = matchesAxiom ? Color.FromArgb(73, 217, 151) : Color.FromArgb(240, 170, 80);
    }

    private void RestorePreviousWindowsDefault()
    {
        var previousId = controllerState.PreviousDefaultOutputId;
        if (string.IsNullOrWhiteSpace(previousId))
        {
            AppendLog("No previous Windows default output has been recorded.");
            return;
        }

        var devices = EnumerateDevices();
        var previous = devices?.FirstOrDefault(device => device.Id.Equals(previousId, StringComparison.OrdinalIgnoreCase));
        if (previous is null)
        {
            AppendLog("Previous Windows default output is not currently available.");
            return;
        }

        try
        {
            using var process = Process.Start(new ProcessStartInfo(consoleExe, "--set-default " + previous.Index)
            {
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            });
            var stdout = process?.StandardOutput.ReadToEnd() ?? "";
            var stderr = process?.StandardError.ReadToEnd() ?? "";
            process?.WaitForExit(3000);
            if (!string.IsNullOrWhiteSpace(stdout)) AppendLog(stdout.Trim());
            if (!string.IsNullOrWhiteSpace(stderr)) AppendLog(stderr.Trim());
            if (process?.ExitCode == 0)
            {
                controllerState.OwnsWindowsDefault = false;
                SaveControllerState();
                SetRouteEvent($"Previous Windows default playback restored: {previous.Name}.");
                UpdateWindowsDefaultStatus();
            }
        }
        catch (Exception exc)
        {
            AppendLog("Failed to restore previous Windows default output: " + exc.Message);
        }
    }

    private void RefreshDevices()
    {
        var savedCapture = controllerState.CaptureIndex;
        var savedOutput = controllerState.OutputIndex;
        var savedCaptureId = controllerState.CaptureId;
        var savedOutputId = controllerState.OutputId;
        var devices = EnumerateDevices();
        if (devices is null) return;

        suppressUiEvents = true;
        try
        {
            ReplaceDeviceItems(devices);
            if (string.IsNullOrWhiteSpace(savedCaptureId))
            {
                SelectPreferredVbCable(captureDevice);
            }
            else
            {
                SelectDevice(captureDevice, savedCaptureId, savedCapture, 0);
            }

            if (string.IsNullOrWhiteSpace(savedOutputId))
            {
                SelectPreferredPhysicalOutput(outputDevice);
            }
            else
            {
                SelectDevice(outputDevice, savedOutputId, savedOutput, 0);
            }
            SelectLatencyMode(controllerState.BufferMs);
        }
        finally
        {
            suppressUiEvents = false;
        }

        var savedRoutePresent = DeviceIdExists(devices, savedCaptureId) && DeviceIdExists(devices, savedOutputId);
        if (savedRoutePresent || string.IsNullOrWhiteSpace(savedCaptureId) || string.IsNullOrWhiteSpace(savedOutputId))
        {
            SaveSelectedRoute();
        }
        else
        {
            AppendLog("One or more saved route endpoints are unavailable. The intended endpoint IDs were preserved for recovery.");
        }
        lastDeviceFingerprint = DeviceFingerprint(devices);
        UpdateStatus();
        RefreshSetupStatus();
        UpdateWindowsDefaultStatus();
        if (controllerState.AutoFollowListeningDevice)
        {
            AutoRouteListeningDevice(restartProcessor: false);
        }
    }

    private List<DeviceInfo>? EnumerateDevices()
    {
        try
        {
            using var process = Process.Start(new ProcessStartInfo(consoleExe, "--list-devices-json")
            {
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            });
            var json = process?.StandardOutput.ReadToEnd() ?? "";
            var stderr = process?.StandardError.ReadToEnd() ?? "";
            process?.WaitForExit(3000);
            if (!string.IsNullOrWhiteSpace(stderr)) AppendLog(stderr.Trim());
            using var doc = JsonDocument.Parse(json);
            var devices = new List<DeviceInfo>();
            foreach (var item in doc.RootElement.GetProperty("devices").EnumerateArray())
            {
                devices.Add(new DeviceInfo(
                    item.GetProperty("index").GetInt32(),
                    item.GetProperty("name").GetString() ?? "",
                    item.GetProperty("id").GetString() ?? ""));
            }
            return devices;
        }
        catch (Exception exc)
        {
            AppendLog("Device refresh failed: " + exc.Message);
            return null;
        }
    }

    private void ReplaceDeviceItems(IReadOnlyList<DeviceInfo> devices)
    {
        captureDevice.Items.Clear();
        outputDevice.Items.Clear();
        foreach (var device in devices)
        {
            captureDevice.Items.Add(device);
            outputDevice.Items.Add(device);
        }
        captureDevice.DisplayMember = nameof(DeviceInfo.Name);
        outputDevice.DisplayMember = nameof(DeviceInfo.Name);
    }

    private void MonitorRouteDevices()
    {
        if (string.IsNullOrWhiteSpace(controllerState.CaptureId) || string.IsNullOrWhiteSpace(controllerState.OutputId)) return;
        var devices = EnumerateDevices();
        if (devices is null) return;

        var fingerprint = DeviceFingerprint(devices);
        var capturePresent = DeviceIdExists(devices, controllerState.CaptureId);
        var outputPresent = DeviceIdExists(devices, controllerState.OutputId);
        if (controllerState.AutoFollowListeningDevice && capturePresent)
        {
            if (AutoRouteListeningDevice(devices, restartProcessor: IsProcessorRunning()))
            {
                lastDeviceFingerprint = fingerprint;
                UpdateWindowsDefaultStatus();
                return;
            }
            outputPresent = DeviceIdExists(devices, controllerState.OutputId);
        }

        if (!capturePresent || !outputPresent)
        {
            if (!routeRecoveryPending)
            {
                restartAfterRouteRecovery = IsProcessorRunning();
                routeRecoveryPending = true;
                var missing = !capturePresent && !outputPresent
                    ? "capture/source and processed output"
                    : !capturePresent ? "capture/source" : "processed output";
                SetRouteEvent($"Waiting for saved {missing} endpoint to reconnect.");
                if (restartAfterRouteRecovery) StopAllProcessors();
            }

            if (fingerprint != lastDeviceFingerprint)
            {
                suppressUiEvents = true;
                try
                {
                    ReplaceDeviceItems(devices);
                    SelectDevice(captureDevice, controllerState.CaptureId, controllerState.CaptureIndex, 0);
                    SelectDevice(outputDevice, controllerState.OutputId, controllerState.OutputIndex, Math.Min(1, Math.Max(0, outputDevice.Items.Count - 1)));
                }
                finally
                {
                    suppressUiEvents = false;
                }
            }
            lastDeviceFingerprint = fingerprint;
            UpdateStatus();
            UpdateWindowsDefaultStatus();
            return;
        }

        if (fingerprint != lastDeviceFingerprint || routeRecoveryPending)
        {
            suppressUiEvents = true;
            try
            {
                ReplaceDeviceItems(devices);
                SelectDevice(captureDevice, controllerState.CaptureId, controllerState.CaptureIndex, 0);
                SelectDevice(outputDevice, controllerState.OutputId, controllerState.OutputIndex, 0);
            }
            finally
            {
                suppressUiEvents = false;
            }
            SaveSelectedRoute();
        }

        if (routeRecoveryPending)
        {
            var shouldRestart = restartAfterRouteRecovery;
            routeRecoveryPending = false;
            restartAfterRouteRecovery = false;
            SetRouteEvent("Route recovery complete: saved endpoints are available again.");
            SetWindowsDefaultToCapture();
            if (shouldRestart) StartProcessor();
        }
        lastDeviceFingerprint = fingerprint;
        UpdateWindowsDefaultStatus();
    }

    private bool AutoRouteListeningDevice(bool restartProcessor)
    {
        var devices = EnumerateDevices();
        return devices is not null && AutoRouteListeningDevice(devices, restartProcessor);
    }

    private bool AutoRouteListeningDevice(IReadOnlyList<DeviceInfo> devices, bool restartProcessor)
    {
        var previousCaptureId = (captureDevice.SelectedItem as DeviceInfo)?.Id ?? controllerState.CaptureId;
        var previousOutputId = (outputDevice.SelectedItem as DeviceInfo)?.Id ?? controllerState.OutputId;
        var previousOutputName = (outputDevice.SelectedItem as DeviceInfo)?.Name ?? "";
        DeviceInfo? selectedCapture = null;
        DeviceInfo? selectedOutput = null;

        suppressUiEvents = true;
        try
        {
            ReplaceDeviceItems(devices);
            if (!SelectPreferredVbCable(captureDevice))
            {
                SelectDevice(captureDevice, controllerState.CaptureId, controllerState.CaptureIndex, 0);
            }

            selectedCapture = captureDevice.SelectedItem as DeviceInfo;
            selectedOutput = SelectBestListeningOutput(outputDevice, selectedCapture);
            if (selectedOutput is null)
            {
                SelectDevice(outputDevice, controllerState.OutputId, controllerState.OutputIndex, 0);
                selectedOutput = outputDevice.SelectedItem as DeviceInfo;
            }
        }
        finally
        {
            suppressUiEvents = false;
        }

        var changed = selectedCapture is not null
            && selectedOutput is not null
            && (!selectedCapture.Id.Equals(previousCaptureId, StringComparison.OrdinalIgnoreCase)
                || !selectedOutput.Id.Equals(previousOutputId, StringComparison.OrdinalIgnoreCase));
        if (!changed) return false;

        SaveSelectedRoute();
        routeRecoveryPending = false;
        restartAfterRouteRecovery = false;
        SetWindowsDefaultToCapture();
        SetRouteEvent($"Auto-routed listening output: {previousOutputName} -> {selectedOutput?.Name}.");
        if (restartProcessor)
        {
            processorFailureState = "Processor restarted after automatic route change.";
            StartProcessor(automaticRestart: true);
        }
        UpdateStatus();
        RefreshSetupStatus();
        return true;
    }

    private static bool DeviceIdExists(IEnumerable<DeviceInfo> devices, string id)
    {
        return !string.IsNullOrWhiteSpace(id) && devices.Any(device => device.Id.Equals(id, StringComparison.OrdinalIgnoreCase));
    }

    private static string DeviceFingerprint(IEnumerable<DeviceInfo> devices)
    {
        return string.Join("|", devices.OrderBy(device => device.Id).Select(device => $"{device.Id}:{device.Index}:{device.Name}"));
    }

    private void RunFirstRunChecks()
    {
        AppendLog("Application root: " + paths.AppRoot);
        AppendLog("Harness root: " + paths.HarnessRoot);
        AppendLog("Data root: " + paths.DataRoot);

        if (!AnyDeviceNameContains("CABLE") && !AnyDeviceNameContains("VB-Audio") && !AnyDeviceNameContains("VB-CABLE"))
        {
            AppendLog("Recommended v1 source endpoint not found: VB-CABLE is not installed or not visible. Steam Streaming Speakers remains available as the current fallback route.");
        }

        if (!File.Exists(consoleExe))
        {
            AppendLog("Processor executable is missing: " + consoleExe);
        }

        if (!File.Exists(runtimeEelPath))
        {
            AppendLog("Runtime Axiom EEL will be generated on save: " + runtimeEelPath);
        }
        RefreshSetupStatus();
    }

    private void ApplyRecommendedSetup()
    {
        RefreshDevices();
        controllerState.BufferMs = 200;
        suppressUiEvents = true;
        try
        {
            SelectLatencyMode(200);
        }
        finally
        {
            suppressUiEvents = false;
        }
        UseVbCableToSelectedOutputRoute();
        RestoreAxiomLiveProg();
        SaveControllerState();
        RefreshSetupStatus();
        AppendLog("Recommended setup applied with VB-CABLE, selected physical output, Axiom LiveProg, and 200 ms resilient buffer.");
    }

    private void CompleteSetup()
    {
        RefreshSetupStatus();
        if (!IsSetupReady())
        {
            AppendLog("Setup is not complete. Resolve the items marked Needs attention on the Setup tab.");
            return;
        }

        controllerState.SetupCompleted = true;
        SaveControllerState();
        setupReadyLabel.Text = "Setup complete";
        setupReadyLabel.ForeColor = Color.FromArgb(73, 217, 151);
        if (mainTabs.TabPages.Count > 0) mainTabs.SelectedIndex = 0;
        AppendLog("First-run setup completed.");
    }

    private void RefreshSetupStatus()
    {
        if (setupFilesLabel.IsDisposed) return;
        var filesReady = File.Exists(consoleExe) && File.Exists(sourceEelPath)
            && File.Exists(testLowCutEelPath) && File.Exists(testPulseGateEelPath);
        var cableReady = captureDevice.Items.Cast<object>()
            .OfType<DeviceInfo>()
            .Any(IsVbCable);
        var capture = captureDevice.SelectedItem as DeviceInfo;
        var output = outputDevice.SelectedItem as DeviceInfo;
        var routeReady = ValidateRoute(capture, output) is null && IsVbCable(capture);
        var profileReady = HasListeningProfile();

        SetSetupCheck(setupFilesLabel, "Application files", filesReady,
            filesReady ? "ready" : "processor, accepted EEL, or test scripts missing");
        SetSetupCheck(setupCableLabel, "VB-CABLE", cableReady,
            cableReady ? "detected" : "not detected");
        SetSetupCheck(setupRouteLabel, "Audio route", routeReady,
            routeReady ? $"{capture?.Name} -> {output?.Name}" : "recommended VB-CABLE route not selected");
        SetSetupCheck(setupProfileLabel, "Listening profile", profileReady,
            profileReady ? "saved" : "not saved");

        var ready = filesReady && cableReady && routeReady && profileReady;
        setupReadyLabel.Text = ready
            ? controllerState.SetupCompleted ? "Setup complete" : "Ready to complete setup"
            : "Setup needs attention";
        setupReadyLabel.ForeColor = ready ? Color.FromArgb(73, 217, 151) : Color.FromArgb(240, 170, 80);
    }

    private bool IsSetupReady()
    {
        var capture = captureDevice.SelectedItem as DeviceInfo;
        var output = outputDevice.SelectedItem as DeviceInfo;
        return File.Exists(consoleExe)
            && File.Exists(sourceEelPath)
            && File.Exists(testLowCutEelPath)
            && File.Exists(testPulseGateEelPath)
            && captureDevice.Items.Cast<object>().OfType<DeviceInfo>().Any(IsVbCable)
            && ValidateRoute(capture, output) is null
            && IsVbCable(capture)
            && HasListeningProfile();
    }

    private bool HasListeningProfile()
    {
        if (!Directory.Exists(profileDir)) return false;
        foreach (var path in Directory.GetFiles(profileDir, "*.json"))
        {
            try
            {
                var profile = ReadProfile(path);
                if (!profile.Type.Equals("qualification", StringComparison.OrdinalIgnoreCase)) return true;
            }
            catch
            {
            }
        }
        return false;
    }

    private static void SetSetupCheck(Label label, string name, bool ready, string detail)
    {
        label.Text = $"{name}: {(ready ? "Ready" : "Needs attention")} - {detail}";
        label.ForeColor = ready ? Color.FromArgb(73, 217, 151) : Color.FromArgb(240, 170, 80);
    }

    private void OpenFolder(string path)
    {
        Directory.CreateDirectory(path);
        Process.Start(new ProcessStartInfo("explorer.exe", Quote(path)) { UseShellExecute = true });
    }

    private void CreateDesktopShortcut()
    {
        try
        {
            var desktop = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
            var shortcutPath = Path.Combine(desktop, "Axiom JamesDSP Controller.lnk");
            var shellType = Type.GetTypeFromProgID("WScript.Shell")
                ?? throw new InvalidOperationException("Windows Script Host is unavailable.");
            dynamic shell = Activator.CreateInstance(shellType)
                ?? throw new InvalidOperationException("Windows Script Host could not be created.");
            dynamic shortcut = shell.CreateShortcut(shortcutPath);
            shortcut.TargetPath = Application.ExecutablePath;
            shortcut.WorkingDirectory = paths.AppRoot;
            shortcut.Description = "Axiom JamesDSP Controller";
            shortcut.IconLocation = Application.ExecutablePath;
            shortcut.Save();
            AppendLog("Desktop shortcut created: " + shortcutPath);
        }
        catch (Exception exc)
        {
            AppendLog("Desktop shortcut creation failed: " + exc.Message);
        }
    }

    private void InitializeTrayIcon()
    {
        var menu = new ContextMenuStrip();
        menu.Items.Add("Open Axiom", null, (_, _) => RestoreFromTray());
        menu.Items.Add("Start Processor", null, (_, _) => StartProcessor());
        menu.Items.Add("Stop Processor", null, (_, _) => StopProcessor());
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("Exit", null, (_, _) =>
        {
            exitRequested = true;
            Close();
        });
        trayIcon = new NotifyIcon
        {
            Text = "Axiom JamesDSP Controller",
            Icon = appIcon,
            ContextMenuStrip = menu,
            Visible = false
        };
        trayIcon.DoubleClick += (_, _) => RestoreFromTray();
    }

    private static Icon LoadAppIcon()
    {
        return Icon.ExtractAssociatedIcon(Application.ExecutablePath) ?? SystemIcons.Application;
    }

    private void RestoreFromTray()
    {
        Show();
        WindowState = FormWindowState.Normal;
        Activate();
        BringToFront();
        trayIcon.Visible = false;
    }

    private CheckBox StateCheckBox(string text, bool initialValue, Action<bool> changed)
    {
        var check = new CheckBox
        {
            Text = text,
            Checked = initialValue,
            AutoSize = true,
            ForeColor = ForeColor,
            Padding = new Padding(4)
        };
        check.CheckedChanged += (_, _) => changed(check.Checked);
        return check;
    }

    private void ConfigureWindowsStartup(bool enabled)
    {
        const string keyPath = @"Software\Microsoft\Windows\CurrentVersion\Run";
        const string valueName = "AxiomJamesDSPController";
        try
        {
            using var key = Microsoft.Win32.Registry.CurrentUser.CreateSubKey(keyPath);
            if (enabled)
            {
                key?.SetValue(valueName, Quote(Application.ExecutablePath));
            }
            else
            {
                key?.DeleteValue(valueName, false);
            }
            AppendLog($"Windows startup launch {(enabled ? "enabled" : "disabled")}.");
        }
        catch (Exception exc)
        {
            AppendLog("Windows startup configuration failed: " + exc.Message);
        }
    }

    private void SeedDataFiles()
    {
        Directory.CreateDirectory(runtimeDir);
        Directory.CreateDirectory(profileDir);
        Directory.CreateDirectory(diagnosticsDir);

        CopyIfMissing(Path.Combine(paths.HarnessRoot, "axiom-liveprog-test.ini"), configPath);
        CopyIfMissing(Path.Combine(paths.HarnessRoot, "runtime", "axiom-test-lowcut.eel"), testLowCutEelPath);
        CopyIfMissing(Path.Combine(paths.HarnessRoot, "runtime", "axiom-test-pulse-gate.eel"), testPulseGateEelPath);
    }

    private static void CopyIfMissing(string source, string destination)
    {
        if (File.Exists(destination) || !File.Exists(source)) return;
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        File.Copy(source, destination);
    }

    private bool AnyDeviceNameContains(string namePart)
    {
        foreach (var item in captureDevice.Items)
        {
            if (item is DeviceInfo device && device.Name.Contains(namePart, StringComparison.OrdinalIgnoreCase)) return true;
        }
        return false;
    }

    private void LoadConfig()
    {
        if (!File.Exists(configPath)) return;
        string section = "";
        foreach (var raw in File.ReadAllLines(configPath))
        {
            var line = raw.Trim();
            if (line.Length == 0 || line.StartsWith(";") || line.StartsWith("#")) continue;
            if (line.StartsWith("[") && line.EndsWith("]"))
            {
                section = line[1..^1];
                if (!config.ContainsKey(section)) config[section] = new(StringComparer.OrdinalIgnoreCase);
                continue;
            }
            var split = line.IndexOf('=');
            if (split < 0 || section.Length == 0) continue;
            config[section][line[..split].Trim()] = line[(split + 1)..].Trim();
        }
    }

    private void LoadControllerState()
    {
        try
        {
            if (File.Exists(statePath))
            {
                controllerState = JsonSerializer.Deserialize<ControllerState>(File.ReadAllText(statePath)) ?? new ControllerState();
            }
        }
        catch
        {
            controllerState = new ControllerState();
        }
    }

    private void LoadAxiomParams()
    {
        if (!File.Exists(sourceEelPath))
        {
            throw new FileNotFoundException(
                "The accepted Axiom EEL resource could not be located. Set AXIOM_ACCEPTED_EEL or reinstall the application package.",
                sourceEelPath);
        }
        var source = File.ReadAllText(sourceEelPath);
        foreach (var metadata in LiveProgMetadataParser.Parse(source).Parameters)
        {
            if (metadata.Default is null) continue;
            var param = new AxiomParam(
                metadata.Key,
                metadata.Description,
                Convert.ToDecimal(metadata.Default.Value, CultureInfo.InvariantCulture),
                Convert.ToDecimal(metadata.Minimum, CultureInfo.InvariantCulture),
                Convert.ToDecimal(metadata.Maximum, CultureInfo.InvariantCulture),
                Convert.ToDecimal(metadata.Step, CultureInfo.InvariantCulture));
            axiomParams.Add(param);
            axiomValues[param.Var] = param.Default;
        }
    }

    private void LoadRuntimeAxiomValues()
    {
        if (!File.Exists(runtimeEelPath)) return;
        try
        {
            var runtime = File.ReadAllText(runtimeEelPath);
            foreach (var param in axiomParams)
            {
                var match = Regex.Match(
                    runtime,
                    $@"(?m)^{Regex.Escape(param.Var)}:(?<value>-?\d+(?:\.\d+)?)<");
                if (match.Success)
                {
                    axiomValues[param.Var] = Decimal(match.Groups["value"].Value);
                }
            }
        }
        catch (Exception exc)
        {
            AppendLog("Existing runtime Axiom values could not be restored: " + exc.Message);
        }
    }

    private void SaveConfigAndRuntimeEel(bool selectAxiomRuntime = false)
    {
        Directory.CreateDirectory(runtimeDir);
        WriteTextIfChanged(runtimeEelPath, BuildRuntimeEel());
        var currentLiveProg = GetValue("LiveProg", "file", "");
        if (selectAxiomRuntime || string.IsNullOrWhiteSpace(currentLiveProg))
        {
            SetValue("LiveProg", "file", runtimeEelPath);
        }
        UpdateLiveProgParameters();
        if (liveProgFileText is not null && liveProgFileText.Text != GetValue("LiveProg", "file", ""))
        {
            liveProgFileText.Text = GetValue("LiveProg", "file", "");
        }
        WriteTextIfChanged(configPath, BuildConfigText());
    }

    private void UpdateLiveProgParameters()
    {
        if (!config.TryGetValue("LiveProg", out var values))
        {
            values = new(StringComparer.OrdinalIgnoreCase);
            config["LiveProg"] = values;
        }
        foreach (var key in values.Keys.Where(key => key.StartsWith("param.", StringComparison.OrdinalIgnoreCase)).ToArray())
        {
            values.Remove(key);
        }

        var selectedPath = GetValue("LiveProg", "file", "");
        if (!Path.GetFullPath(selectedPath).Equals(Path.GetFullPath(runtimeEelPath), StringComparison.OrdinalIgnoreCase)) return;
        foreach (var param in axiomParams)
        {
            values["param." + param.Var] = axiomValues.GetValueOrDefault(param.Var, param.Default)
                .ToString(CultureInfo.InvariantCulture);
        }
    }

    private static void WriteTextIfChanged(string path, string content)
    {
        if (File.Exists(path) && File.ReadAllText(path).Equals(content, StringComparison.Ordinal)) return;
        File.WriteAllText(path, content, Encoding.UTF8);
    }

    private void LoadLowCutTest()
    {
        LoadLiveProgTest(testLowCutEelPath, "low-cut");
    }

    private void LoadPulseGateTest()
    {
        LoadLiveProgTest(testPulseGateEelPath, "pulse-gate");
    }

    private void LoadLiveProgTest(string path, string label)
    {
        if (!File.Exists(path))
        {
            AppendLog($"{label} test script is missing: " + path);
            return;
        }
        var validationError = ValidateLiveProgScript(path, requireUiDefaults: true);
        if (validationError is not null)
        {
            AppendLog($"{label} test script validation failed: {validationError}");
            return;
        }
        SetValue("LiveProg", "enabled", "true");
        SetValue("LiveProg", "file", path);
        if (liveProgFileText is not null) liveProgFileText.Text = path;
        File.WriteAllText(configPath, BuildConfigText(), Encoding.UTF8);
        AppendLog($"Loaded {label} LiveProg test script. Use this to confirm that Axiom/JamesDSP is audibly in the path.");
        StartProcessorIfReady($"{label} confidence test");
    }

    private void RestoreAxiomLiveProg()
    {
        SaveConfigAndRuntimeEel(selectAxiomRuntime: true);
        AppendLog("Restored generated Axiom LiveProg script.");
    }

    private void RestoreAxiomAndStart()
    {
        RestoreAxiomLiveProg();
        StartProcessorIfReady("Axiom restore");
    }

    private void StartProcessorIfReady(string reason)
    {
        if (IsProcessorRunning()) return;
        var routeError = ValidateRoute(captureDevice.SelectedItem as DeviceInfo, outputDevice.SelectedItem as DeviceInfo);
        var scriptError = ValidateLiveProgScript(GetValue("LiveProg", "file", ""));
        if (routeError is not null || scriptError is not null)
        {
            AppendLog($"{reason} loaded, but processor start is blocked: {routeError ?? scriptError}");
            return;
        }
        AppendLog($"{reason} starting processor.");
        StartProcessor();
    }

    private void SaveProfile(string type)
    {
        Directory.CreateDirectory(profileDir);
        var profile = CaptureProfile(type == "listening" ? "Axiom Listening Profile" : "Axiom Profile", type);
        var path = ProfilePath(type);
        WriteProfile(path, profile);
        lastProfileName = profile.Name;
        profileDirty = false;
        AppendLog($"Saved {type} profile: {path}");
        UpdateStatus();
        RefreshProfileList(path);
        RefreshSetupStatus();
    }

    private void LoadProfile(string type)
    {
        var path = ProfilePath(type);
        if (!File.Exists(path))
        {
            AppendLog($"{type} profile does not exist yet: {path}");
            return;
        }

        try
        {
            var profile = ReadProfile(path);
            ApplyProfile(profile);
            lastProfileName = profile.Name;
            profileDirty = false;
            AppendLog($"Loaded {type} profile schema v{profile.SchemaVersion}: {path}");
        }
        catch (Exception exc)
        {
            AppendLog($"Failed to load {type} profile: {exc.Message}");
        }
        UpdateStatus();
    }

    private void RefreshProfileList(string? selectPath = null)
    {
        if (profileSelector.IsDisposed) return;
        Directory.CreateDirectory(profileDir);
        var items = new List<ProfileListItem>();
        foreach (var path in Directory.GetFiles(profileDir, "*.json").OrderBy(path => path))
        {
            try
            {
                var profile = ReadProfile(path);
                var name = string.IsNullOrWhiteSpace(profile.Name) ? Path.GetFileNameWithoutExtension(path) : profile.Name;
                var type = string.IsNullOrWhiteSpace(profile.Type) ? "listening" : profile.Type;
                items.Add(new ProfileListItem(path, name, type));
            }
            catch (Exception exc)
            {
                AppendLog($"Profile skipped because it could not be read: {path}: {exc.Message}");
            }
        }

        profileSelector.Items.Clear();
        foreach (var item in items.OrderBy(item => item.Type.Equals("qualification", StringComparison.OrdinalIgnoreCase) ? 1 : 0).ThenBy(item => item.Name))
        {
            profileSelector.Items.Add(item);
        }

        if (profileSelector.Items.Count > 0)
        {
            var selectedIndex = 0;
            if (!string.IsNullOrWhiteSpace(selectPath))
            {
                for (var i = 0; i < profileSelector.Items.Count; i++)
                {
                    if (profileSelector.Items[i] is ProfileListItem item
                        && item.Path.Equals(selectPath, StringComparison.OrdinalIgnoreCase))
                    {
                        selectedIndex = i;
                        break;
                    }
                }
            }
            profileSelector.SelectedIndex = selectedIndex;
        }
        UpdateProfileManagerStatus();
    }

    private void SaveNamedProfile()
    {
        var name = profileNameText.Text.Trim();
        if (string.IsNullOrWhiteSpace(name))
        {
            AppendLog("Profile name is required.");
            return;
        }

        Directory.CreateDirectory(profileDir);
        var path = Path.Combine(profileDir, $"user-{Guid.NewGuid():N}.json");
        var profile = CaptureProfile(name, "listening");
        WriteProfile(path, profile);
        lastProfileName = profile.Name;
        profileDirty = false;
        profileDirty = false;
        AppendLog("Saved new profile: " + path);
        RefreshProfileList(path);
        RefreshSetupStatus();
    }

    private void UpdateSelectedProfile()
    {
        if (profileSelector.SelectedItem is not ProfileListItem item) return;
        if (IsProtectedProfile(item))
        {
            AppendLog("Qualification is protected and cannot be overwritten.");
            return;
        }
        var profile = CaptureProfile(item.Name, "listening");
        WriteProfile(item.Path, profile);
        lastProfileName = profile.Name;
        profileDirty = false;
        AppendLog("Updated profile: " + item.Name);
        RefreshProfileList(item.Path);
    }

    private void LoadSelectedProfile()
    {
        if (profileSelector.SelectedItem is not ProfileListItem item) return;
        try
        {
            var profile = ReadProfile(item.Path);
            ApplyProfile(profile);
            lastProfileName = profile.Name;
            profileDirty = false;
            AppendLog("Loaded profile: " + profile.Name);
        }
        catch (Exception exc)
        {
            AppendLog("Profile load failed: " + exc.Message);
        }
        UpdateStatus();
        UpdateProfileManagerStatus();
    }

    private void DuplicateSelectedProfile()
    {
        if (profileSelector.SelectedItem is not ProfileListItem item) return;
        try
        {
            var source = ReadProfile(item.Path);
            source.Name = profileNameText.Text.Trim();
            if (string.IsNullOrWhiteSpace(source.Name)) source.Name = item.Name + " Copy";
            source.Type = "listening";
            source.SavedAtUtc = DateTime.UtcNow;
            var path = Path.Combine(profileDir, $"user-{Guid.NewGuid():N}.json");
            WriteProfile(path, source);
            AppendLog("Duplicated profile: " + source.Name);
            RefreshProfileList(path);
        }
        catch (Exception exc)
        {
            AppendLog("Profile duplication failed: " + exc.Message);
        }
    }

    private void RenameSelectedProfile()
    {
        if (profileSelector.SelectedItem is not ProfileListItem item) return;
        if (IsProtectedProfile(item))
        {
            AppendLog("Qualification is protected and cannot be renamed.");
            return;
        }
        var name = profileNameText.Text.Trim();
        if (string.IsNullOrWhiteSpace(name))
        {
            AppendLog("Profile name is required.");
            return;
        }
        try
        {
            var profile = ReadProfile(item.Path);
            profile.Name = name;
            profile.SavedAtUtc = DateTime.UtcNow;
            WriteProfile(item.Path, profile);
            if (lastProfileName.Equals(item.Name, StringComparison.OrdinalIgnoreCase)) lastProfileName = name;
            AppendLog($"Renamed profile to: {name}");
            RefreshProfileList(item.Path);
        }
        catch (Exception exc)
        {
            AppendLog("Profile rename failed: " + exc.Message);
        }
    }

    private void DeleteSelectedProfile()
    {
        if (profileSelector.SelectedItem is not ProfileListItem item) return;
        if (IsProtectedProfile(item))
        {
            AppendLog("Qualification is protected and cannot be deleted.");
            return;
        }
        if (MessageBox.Show(this, $"Delete profile '{item.Name}'?", "Delete Profile", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes) return;
        File.Delete(item.Path);
        AppendLog("Deleted profile: " + item.Name);
        RefreshProfileList();
        RefreshSetupStatus();
    }

    private void ImportProfile()
    {
        using var dialog = new OpenFileDialog { Filter = "Axiom profiles (*.json)|*.json|All files (*.*)|*.*" };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        try
        {
            var profile = ReadProfile(dialog.FileName);
            profile.Type = "listening";
            profile.SavedAtUtc = DateTime.UtcNow;
            if (string.IsNullOrWhiteSpace(profile.Name)) profile.Name = Path.GetFileNameWithoutExtension(dialog.FileName);
            var path = Path.Combine(profileDir, $"user-{Guid.NewGuid():N}.json");
            WriteProfile(path, profile);
            AppendLog("Imported profile: " + profile.Name);
            RefreshProfileList(path);
        }
        catch (Exception exc)
        {
            AppendLog("Profile import failed: " + exc.Message);
        }
    }

    private void ExportSelectedProfile()
    {
        if (profileSelector.SelectedItem is not ProfileListItem item) return;
        using var dialog = new SaveFileDialog
        {
            Filter = "Axiom profiles (*.json)|*.json",
            FileName = SanitizeFileName(item.Name) + ".json"
        };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        File.Copy(item.Path, dialog.FileName, true);
        AppendLog("Exported profile: " + dialog.FileName);
    }

    private AxiomProfile ReadProfile(string path)
    {
        var profile = JsonSerializer.Deserialize<AxiomProfile>(File.ReadAllText(path))
            ?? throw new InvalidDataException("Profile JSON did not contain an object.");
        profile.Config ??= new(StringComparer.OrdinalIgnoreCase);
        profile.AxiomValues ??= new(StringComparer.OrdinalIgnoreCase);
        NormalizePortableProfilePaths(profile);
        return profile;
    }

    private static bool IsProtectedProfile(ProfileListItem item)
    {
        return item.Type.Equals("qualification", StringComparison.OrdinalIgnoreCase);
    }

    private static string SanitizeFileName(string value)
    {
        var invalid = Path.GetInvalidFileNameChars();
        var cleaned = new string(value.Select(ch => invalid.Contains(ch) ? '_' : ch).ToArray()).Trim();
        return string.IsNullOrWhiteSpace(cleaned) ? "axiom-profile" : cleaned;
    }

    private void UpdateProfileManagerStatus()
    {
        if (profileManagerStatusLabel.IsDisposed) return;
        var selected = profileSelector.SelectedItem as ProfileListItem;
        var selection = selected is null ? "none selected" : selected.ToString();
        profileManagerStatusLabel.Text = $"Profile state: {lastProfileName}{(profileDirty ? " (unsaved changes)" : "")}; selected: {selection}";
        profileManagerStatusLabel.ForeColor = profileDirty ? Color.FromArgb(240, 170, 80) : Color.FromArgb(73, 217, 151);
    }

    private void LoadQualificationBaseline()
    {
        var profile = CaptureProfile("Axiom Qualification Baseline", "qualification");
        profile.BufferMs = 200;
        foreach (var section in new[] { "BassBoost", "StereoWide", "Reverb", "Tube", "Compressor", "Equalizer", "Crossfeed", "DDC", "Convolver" })
        {
            SetProfileValue(profile, section, "enabled", "false");
        }
        SetProfileValue(profile, "General", "postGain", "0");
        SetProfileValue(profile, "Crossfeed", "mode", "0");
        SetProfileValue(profile, "LiveProg", "enabled", "true");
        SetProfileValue(profile, "LiveProg", "file", runtimeEelPath);
        profile.LiveProgFile = runtimeEelPath;
        profile.LiveProgEnabled = true;
        profile.PostGain = 0;
        profile.CrossfeedEnabled = false;
        profile.CrossfeedMode = 0;
        profile.AxiomValues = axiomParams.ToDictionary(param => param.Var, param => param.Default, StringComparer.OrdinalIgnoreCase);
        ApplyProfile(profile);
        SaveConfigAndRuntimeEel(selectAxiomRuntime: true);
        lastProfileName = profile.Name;
        Directory.CreateDirectory(profileDir);
        WriteProfile(ProfilePath("qualification"), profile);
        AppendLog("Loaded qualification baseline: accepted Axiom defaults, host effects disabled, post gain 0 dB, Crossfeed disabled, 200 ms resilient buffer.");
        UpdateStatus();
    }

    private AxiomProfile CaptureProfile(string name, string type)
    {
        return new AxiomProfile
        {
            SchemaVersion = 2,
            Name = name,
            Type = type,
            SavedAtUtc = DateTime.UtcNow,
            CaptureIndex = (captureDevice.SelectedItem as DeviceInfo)?.Index ?? controllerState.CaptureIndex,
            OutputIndex = (outputDevice.SelectedItem as DeviceInfo)?.Index ?? controllerState.OutputIndex,
            CaptureId = (captureDevice.SelectedItem as DeviceInfo)?.Id ?? controllerState.CaptureId,
            OutputId = (outputDevice.SelectedItem as DeviceInfo)?.Id ?? controllerState.OutputId,
            BufferMs = controllerState.BufferMs,
            LiveProgFile = GetValue("LiveProg", "file", ""),
            LiveProgEnabled = GetBool("LiveProg", "enabled", true),
            PostGain = GetDecimal("General", "postGain", 0),
            CrossfeedEnabled = GetBool("Crossfeed", "enabled", false),
            CrossfeedMode = GetInt("Crossfeed", "mode", 0),
            Config = CloneConfig(config),
            AxiomValues = new Dictionary<string, decimal>(axiomValues, StringComparer.OrdinalIgnoreCase)
        };
    }

    private void ApplyProfile(AxiomProfile profile)
    {
        suppressUiEvents = true;
        try
        {
            controllerState.BufferMs = profile.BufferMs <= 0 ? 200 : profile.BufferMs;
            SelectDevice(captureDevice, profile.CaptureId, profile.CaptureIndex, captureDevice.SelectedIndex);
            SelectDevice(outputDevice, profile.OutputId, profile.OutputIndex, outputDevice.SelectedIndex);
            SelectLatencyMode(controllerState.BufferMs);
            if (profile.Config.Count > 0)
            {
                ReplaceConfig(profile.Config);
            }
            else
            {
                SetValue("LiveProg", "enabled", profile.LiveProgEnabled ? "true" : "false");
                SetValue("LiveProg", "file", string.IsNullOrWhiteSpace(profile.LiveProgFile) ? runtimeEelPath : profile.LiveProgFile);
                SetValue("General", "postGain", profile.PostGain.ToString(CultureInfo.InvariantCulture));
                SetValue("Crossfeed", "enabled", profile.CrossfeedEnabled ? "true" : "false");
                SetValue("Crossfeed", "mode", profile.CrossfeedMode.ToString(CultureInfo.InvariantCulture));
            }
            if (profile.AxiomValues.Count > 0)
            {
                foreach (var param in axiomParams)
                {
                    axiomValues[param.Var] = profile.AxiomValues.TryGetValue(param.Var, out var value) ? value : param.Default;
                }
            }
            RefreshConfigControls();
            RefreshAxiomControls();
        }
        finally
        {
            suppressUiEvents = false;
        }
        SaveSelectedRoute();
        SaveConfigAndRuntimeEel();
        profileDirty = false;
        UpdateProfileManagerStatus();
    }

    private void RefreshConfigControls()
    {
        foreach (var (key, input) in numericControls)
        {
            var (section, configKey) = SplitConfigKey(key);
            input.Value = Math.Clamp(GetDecimal(section, configKey, input.Value), input.Minimum, input.Maximum);
        }

        foreach (var (key, check) in checkControls)
        {
            var (section, configKey) = SplitConfigKey(key);
            check.Checked = GetBool(section, configKey, check.Checked);
        }

        foreach (var (key, combo) in comboControls)
        {
            var (section, configKey) = SplitConfigKey(key);
            if (combo.Items.Count > 0) combo.SelectedIndex = Math.Clamp(GetInt(section, configKey, combo.SelectedIndex), 0, combo.Items.Count - 1);
        }

        foreach (var (key, text) in textControls)
        {
            var (section, configKey) = SplitConfigKey(key);
            text.Text = GetValue(section, configKey, text.Text);
        }
    }

    private string ProfilePath(string type) => Path.Combine(profileDir, $"axiom-{type}-profile.json");

    private static Dictionary<string, Dictionary<string, string>> CloneConfig(
        Dictionary<string, Dictionary<string, string>> source)
    {
        var clone = new Dictionary<string, Dictionary<string, string>>(StringComparer.OrdinalIgnoreCase);
        foreach (var (section, values) in source)
        {
            clone[section] = new Dictionary<string, string>(values, StringComparer.OrdinalIgnoreCase);
        }
        return clone;
    }

    private void ReplaceConfig(Dictionary<string, Dictionary<string, string>> source)
    {
        config.Clear();
        foreach (var (section, values) in source)
        {
            config[section] = new Dictionary<string, string>(values, StringComparer.OrdinalIgnoreCase);
        }
    }

    private static void SetProfileValue(AxiomProfile profile, string section, string key, string value)
    {
        if (!profile.Config.TryGetValue(section, out var values))
        {
            values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            profile.Config[section] = values;
        }
        values[key] = value;
    }

    private static void WriteProfile(string path, AxiomProfile profile)
    {
        var tempPath = path + ".tmp";
        File.WriteAllText(tempPath, JsonSerializer.Serialize(profile, new JsonSerializerOptions { WriteIndented = true }), Encoding.UTF8);
        File.Move(tempPath, path, true);
    }

    private void NormalizePortableConfigPaths()
    {
        var configured = GetValue("LiveProg", "file", "");
        SetValue("LiveProg", "file", ResolvePortableLiveProgPath(configured));
    }

    private void NormalizePortableProfilePaths(AxiomProfile profile)
    {
        profile.LiveProgFile = ResolvePortableLiveProgPath(profile.LiveProgFile);
        if (profile.Config.TryGetValue("LiveProg", out var liveProg)
            && liveProg.TryGetValue("file", out var configured))
        {
            liveProg["file"] = ResolvePortableLiveProgPath(configured);
        }
    }

    private string ResolvePortableLiveProgPath(string configured)
    {
        if (!string.IsNullOrWhiteSpace(configured) && File.Exists(configured)) return configured;

        var fileName = string.IsNullOrWhiteSpace(configured) ? "" : Path.GetFileName(configured);
        if (!string.IsNullOrWhiteSpace(fileName))
        {
            var dataCandidate = Path.Combine(runtimeDir, fileName);
            if (File.Exists(dataCandidate)) return dataCandidate;

            var harnessCandidate = Path.Combine(paths.HarnessRoot, "runtime", fileName);
            if (File.Exists(harnessCandidate))
            {
                CopyIfMissing(harnessCandidate, dataCandidate);
                if (File.Exists(dataCandidate)) return dataCandidate;
                return harnessCandidate;
            }
        }

        return runtimeEelPath;
    }

    private void RefreshAxiomControls()
    {
        foreach (var param in axiomParams)
        {
            if (!axiomControls.TryGetValue(param.Var, out var input)) continue;
            var value = axiomValues.GetValueOrDefault(param.Var, param.Default);
            input.Value = Math.Clamp(value, input.Minimum, input.Maximum);
        }
    }

    private void ExportDiagnosticReport()
    {
        Directory.CreateDirectory(diagnosticsDir);
        var stamp = DateTime.Now.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture);
        var path = Path.Combine(diagnosticsDir, $"axiom-diagnostics-{stamp}.txt");
        var report = BuildDiagnosticReport();
        File.WriteAllText(path, report, Encoding.UTF8);
        diagnosticsBox.Text = report;
        AppendLog("Diagnostic report exported: " + path);
    }

    private void CopyDiagnosticSummary()
    {
        var report = BuildDiagnosticReport();
        Clipboard.SetText(report);
        diagnosticsBox.Text = report;
        AppendLog("Diagnostic summary copied to clipboard.");
    }

    private void OpenHealthHistory()
    {
        Directory.CreateDirectory(diagnosticsDir);
        if (!File.Exists(healthHistoryPath)) File.WriteAllText(healthHistoryPath, "", Encoding.UTF8);
        Process.Start(new ProcessStartInfo("notepad.exe", Quote(healthHistoryPath)) { UseShellExecute = true });
    }

    private void ClearHealthHistory()
    {
        if (MessageBox.Show(this, "Clear the persistent audio health history?", "Clear Health History", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes) return;
        Directory.CreateDirectory(diagnosticsDir);
        File.WriteAllText(healthHistoryPath, "", Encoding.UTF8);
        AppendLog("Persistent health history cleared.");
    }

    private void RecordHealthSample(HealthSample sample)
    {
        try
        {
            Directory.CreateDirectory(diagnosticsDir);
            File.AppendAllText(healthHistoryPath, JsonSerializer.Serialize(sample) + Environment.NewLine, Encoding.UTF8);
        }
        catch (Exception exc)
        {
            AppendLog("Health history write failed: " + exc.Message);
        }

        var warnings = new List<string>();
        if (healthCounterBaselineEstablished)
        {
            if (sample.Dropped > previousDropped) warnings.Add($"+{sample.Dropped - previousDropped} dropped frames");
            if (sample.ConversionErrors > previousConversionErrors) warnings.Add($"+{sample.ConversionErrors - previousConversionErrors} conversion errors");
            if (sample.Discontinuities > previousDiscontinuities) warnings.Add($"+{sample.Discontinuities - previousDiscontinuities} capture discontinuities");
            if (sample.RenderStarvations > previousRenderStarvations) warnings.Add($"+{sample.RenderStarvations - previousRenderStarvations} render starvations");
            if (sample.RenderErrors > previousRenderErrors) warnings.Add($"+{sample.RenderErrors - previousRenderErrors} render errors");
            var intervalDspCalls = sample.DspCalls - previousDspCalls;
            var intervalDeadlineMisses = sample.DspDeadlineMisses - previousDspDeadlineMisses;
            if (intervalDspCalls > 0 && intervalDeadlineMisses / (double)intervalDspCalls > 0.10)
            {
                warnings.Add($"DSP deadline pressure {intervalDeadlineMisses}/{intervalDspCalls}");
            }
            if (sample.DspCriticalStalls > previousDspCriticalStalls) warnings.Add($"+{sample.DspCriticalStalls - previousDspCriticalStalls} critical DSP stalls");
        }

        previousDropped = sample.Dropped;
        previousConversionErrors = sample.ConversionErrors;
        previousDiscontinuities = sample.Discontinuities;
        previousRenderStarvations = sample.RenderStarvations;
        previousRenderErrors = sample.RenderErrors;
        previousDspCalls = sample.DspCalls;
        previousDspDeadlineMisses = sample.DspDeadlineMisses;
        previousDspCriticalStalls = sample.DspCriticalStalls;
        healthCounterBaselineEstablished = true;

        if (warnings.Count > 0)
        {
            sessionWarningCount += 1;
            AppendLog("[HEALTH WARNING] " + string.Join("; ", warnings));
        }
    }

    private void ResetHealthCounterBaseline()
    {
        previousDropped = 0;
        previousConversionErrors = 0;
        previousDiscontinuities = 0;
        previousRenderStarvations = 0;
        previousRenderErrors = 0;
        previousDspCalls = 0;
        previousDspDeadlineMisses = 0;
        previousDspCriticalStalls = 0;
        healthCounterBaselineEstablished = false;
    }

    private void WriteSessionSummary()
    {
        try
        {
            Directory.CreateDirectory(diagnosticsDir);
            var path = Path.Combine(diagnosticsDir, $"session-summary-{DateTime.Now:yyyyMMdd-HHmmss}.txt");
            var summary = new StringBuilder()
                .AppendLine("Axiom JamesDSP Controller Session Summary")
                .AppendLine("Started: " + sessionStartedAt.ToString("O", CultureInfo.InvariantCulture))
                .AppendLine("Ended: " + DateTime.Now.ToString("O", CultureInfo.InvariantCulture))
                .AppendLine("Profile: " + lastProfileName)
                .AppendLine("Buffer: " + controllerState.BufferMs + " ms")
                .AppendLine("Health warning intervals: " + sessionWarningCount)
                .AppendLine(lastAudioHealth)
                .AppendLine(lastPerformanceHealth)
                .ToString();
            File.WriteAllText(path, summary, Encoding.UTF8);
        }
        catch
        {
        }
    }

    private string BuildDiagnosticReport()
    {
        var capture = captureDevice.SelectedItem as DeviceInfo;
        var output = outputDevice.SelectedItem as DeviceInfo;
        var sb = new StringBuilder();
        sb.AppendLine("Axiom JamesDSP Controller Diagnostic Report");
        sb.AppendLine("Generated: " + DateTime.Now.ToString("O", CultureInfo.InvariantCulture));
        sb.AppendLine();
        sb.AppendLine("Route");
        sb.AppendLine("  State: " + BuildRouteStateText(
            CountRunningProcessors(),
            ValidateRoute(capture, output),
            ValidateLiveProgScript(GetValue("LiveProg", "file", ""))));
        sb.AppendLine("  Capture/source: " + FormatDevice(capture));
        sb.AppendLine("  Processed output: " + FormatDevice(output));
        sb.AppendLine("  Recommendation: " + RouteRecommendation(capture, output));
        sb.AppendLine("  Last event: " + lastRouteEvent);
        sb.AppendLine();
        sb.AppendLine("Processor");
        sb.AppendLine("  Running: " + IsProcessorRunning());
        sb.AppendLine("  Buffer: " + controllerState.BufferMs + " ms");
        sb.AppendLine("  Command: " + lastProcessorCommand);
        sb.AppendLine();
        sb.AppendLine("Script");
        sb.AppendLine("  LiveProg enabled: " + GetBool("LiveProg", "enabled", true));
        sb.AppendLine("  LiveProg file: " + GetValue("LiveProg", "file", ""));
        sb.AppendLine("  Validation: " + (ValidateLiveProgScript(GetValue("LiveProg", "file", "")) ?? "ok"));
        sb.AppendLine();
        sb.AppendLine("Profile");
        sb.AppendLine("  Current: " + lastProfileName);
        sb.AppendLine("  Post gain: " + GetValue("General", "postGain", "0") + " dB");
        sb.AppendLine("  Crossfeed: " + GetValue("Crossfeed", "enabled", "false") + " mode " + GetValue("Crossfeed", "mode", "0"));
        sb.AppendLine();
        sb.AppendLine("Audio Health");
        sb.AppendLine("  Capture format: " + lastCaptureFormat);
        sb.AppendLine("  Render format: " + lastRenderFormat);
        sb.AppendLine("  Buffer status: " + lastBufferStatus);
        sb.AppendLine("  " + lastAudioHealth);
        sb.AppendLine("  " + lastPerformanceHealth);
        sb.AppendLine("  Session warning intervals: " + sessionWarningCount);
        sb.AppendLine("  Persistent history: " + healthHistoryPath);
        sb.AppendLine();
        sb.AppendLine("Recent Log");
        sb.AppendLine(logBox.Text);
        return sb.ToString();
    }

    private string BuildRuntimeEel()
    {
        var text = File.ReadAllText(sourceEelPath);
        foreach (var (varName, value) in axiomValues)
        {
            var formatted = value.ToString(CultureInfo.InvariantCulture);
            text = Regex.Replace(text, $@"^{Regex.Escape(varName)}:(-?\d+(?:\.\d+)?)(<)", $"{varName}:{formatted}$2", RegexOptions.Multiline);
            text = Regex.Replace(text, $@"\b{Regex.Escape(varName)}\s*=\s*-?\d+(?:\.\d+)?\s*;", $"{varName} = {formatted};", RegexOptions.Multiline);
        }
        return text;
    }

    private string BuildConfigText()
    {
        string[] sections = { "General", "BassBoost", "StereoWide", "Reverb", "Tube", "Compressor", "Equalizer", "Crossfeed", "DDC", "Convolver", "LiveProg" };
        var sb = new StringBuilder();
        sb.AppendLine("; Generated by Axiom JamesDSP Controller.");
        sb.AppendLine();
        foreach (var section in sections)
        {
            if (!config.TryGetValue(section, out var values)) continue;
            sb.AppendLine($"[{section}]");
            foreach (var pair in values) sb.AppendLine($"{pair.Key} = {pair.Value}");
            sb.AppendLine();
        }
        return sb.ToString();
    }

    private void QueueSave()
    {
        saveTimer.Stop();
        saveTimer.Start();
        profileDirty = true;
        UpdateProfileManagerStatus();
        if (processor is not { HasExited: false })
        {
            AppendLog("Settings saved, but processor is stopped. Click Start Processor to hear changes.");
        }
    }

    private void SaveSelectedRoute()
    {
        if (!suppressUiEvents) profileDirty = true;
        if (captureDevice.SelectedItem is DeviceInfo capture)
        {
            controllerState.CaptureIndex = capture.Index;
            controllerState.CaptureId = capture.Id;
        }
        if (outputDevice.SelectedItem is DeviceInfo output)
        {
            controllerState.OutputIndex = output.Index;
            controllerState.OutputId = output.Id;
        }
        SaveControllerState();
        UpdateStatus();
        UpdateProfileManagerStatus();
    }

    private void SaveControllerState()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(statePath)!);
            File.WriteAllText(statePath, JsonSerializer.Serialize(controllerState, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch (Exception exc)
        {
            AppendLog("Route state save failed: " + exc.Message);
        }
    }

    private int SelectedBufferMs()
    {
        return latencyMode.SelectedIndex switch
        {
            0 => 30,
            1 => 60,
            3 => 150,
            4 => 200,
            _ => 100
        };
    }

    private void SelectLatencyMode(int bufferMs)
    {
        latencyMode.SelectedIndex = bufferMs switch
        {
            <= 30 => 0,
            <= 60 => 1,
            >= 200 => 4,
            >= 150 => 3,
            _ => 2
        };
        controllerState.BufferMs = SelectedBufferMs();
        bufferLabel.Text = $"Buffer: {controllerState.BufferMs} ms selected";
    }

    private bool IsProcessorRunning()
    {
        var running = CountRunningProcessors() > 0;
        return running;
    }

    private static int CountRunningProcessors()
    {
        var processes = Process.GetProcessesByName("AxiomJamesDSPConsole");
        var count = processes.Length;
        foreach (var process in processes) process.Dispose();
        return count;
    }

    private static void SelectDevice(ComboBox combo, string savedId, int savedIndex, int fallbackSelectedIndex)
    {
        if (!string.IsNullOrWhiteSpace(savedId))
        {
            for (var i = 0; i < combo.Items.Count; i++)
            {
                if (combo.Items[i] is DeviceInfo device && device.Id.Equals(savedId, StringComparison.OrdinalIgnoreCase))
                {
                    combo.SelectedIndex = i;
                    return;
                }
            }
        }

        for (var i = 0; i < combo.Items.Count; i++)
        {
            if (combo.Items[i] is DeviceInfo device && device.Index == savedIndex)
            {
                combo.SelectedIndex = i;
                return;
            }
        }

        if (combo.Items.Count > 0) combo.SelectedIndex = Math.Clamp(fallbackSelectedIndex, 0, combo.Items.Count - 1);
    }

    private static void SelectDevice(ComboBox combo, int savedIndex, int fallbackSelectedIndex)
    {
        SelectDevice(combo, "", savedIndex, fallbackSelectedIndex);
    }

    private static bool SelectPreferredVbCable(ComboBox combo)
    {
        return SelectDeviceByExactName(combo, "CABLE Input (VB-Audio Virtual Cable)")
            || SelectDeviceByName(combo, "CABLE Input")
            || SelectDeviceByName(combo, "VB-CABLE");
    }

    private static bool SelectPreferredPhysicalOutput(ComboBox combo)
    {
        if (SelectDeviceByName(combo, "EarPods")) return true;
        if (SelectDeviceByName(combo, "Headphones")) return true;
        if (SelectDeviceByName(combo, "Headset")) return true;
        if (SelectDeviceByName(combo, "USB")) return true;
        if (SelectDeviceByName(combo, "Bluetooth")) return true;
        if (SelectDeviceByName(combo, "Realtek")) return true;
        for (var i = 0; i < combo.Items.Count; i++)
        {
            if (combo.Items[i] is DeviceInfo device && !IsVirtualSource(device))
            {
                combo.SelectedIndex = i;
                return true;
            }
        }
        return false;
    }

    private static DeviceInfo? SelectBestListeningOutput(ComboBox combo, DeviceInfo? capture)
    {
        DeviceInfo? best = null;
        var bestScore = int.MinValue;
        var bestItemIndex = -1;
        for (var i = 0; i < combo.Items.Count; i++)
        {
            if (combo.Items[i] is not DeviceInfo candidate) continue;
            if (capture is not null && candidate.Id.Equals(capture.Id, StringComparison.OrdinalIgnoreCase)) continue;
            var score = ListeningOutputScore(candidate);
            if (score <= 0) continue;
            if (score > bestScore)
            {
                best = candidate;
                bestScore = score;
                bestItemIndex = i;
            }
        }

        if (bestItemIndex >= 0) combo.SelectedIndex = bestItemIndex;
        return best;
    }

    private static int ListeningOutputScore(DeviceInfo device)
    {
        if (IsVirtualSource(device)) return 0;
        var name = device.Name;
        if (name.Contains("EarPods", StringComparison.OrdinalIgnoreCase)) return 1000;
        if (name.Contains("Headphones", StringComparison.OrdinalIgnoreCase)) return 950;
        if (name.Contains("Headset", StringComparison.OrdinalIgnoreCase)) return 940;
        if (name.Contains("USB", StringComparison.OrdinalIgnoreCase)) return 900;
        if (name.Contains("Bluetooth", StringComparison.OrdinalIgnoreCase)) return 850;
        if (name.Contains("Realtek", StringComparison.OrdinalIgnoreCase)) return 600;
        if (name.Contains("Speaker", StringComparison.OrdinalIgnoreCase)) return 500;
        if (name.Contains("AUX", StringComparison.OrdinalIgnoreCase)) return 450;
        return 300;
    }

    private static bool SelectDeviceByExactName(ComboBox combo, string name)
    {
        for (var i = 0; i < combo.Items.Count; i++)
        {
            if (combo.Items[i] is DeviceInfo device && device.Name.Equals(name, StringComparison.OrdinalIgnoreCase))
            {
                combo.SelectedIndex = i;
                return true;
            }
        }
        return false;
    }

    private static bool SelectDeviceByName(ComboBox combo, string namePart)
    {
        for (var i = 0; i < combo.Items.Count; i++)
        {
            if (combo.Items[i] is DeviceInfo device && device.Name.Contains(namePart, StringComparison.OrdinalIgnoreCase))
            {
                combo.SelectedIndex = i;
                return true;
            }
        }
        return false;
    }

    private static string FormatDevice(DeviceInfo? device)
    {
        return device is null ? "(none)" : $"[{device.Index}] {device.Name}";
    }

    private static bool IsVirtualSource(DeviceInfo? device)
    {
        if (device is null) return false;
        return device.Name.Contains("CABLE", StringComparison.OrdinalIgnoreCase)
            || device.Name.Contains("VB-Audio", StringComparison.OrdinalIgnoreCase)
            || device.Name.Contains("Steam Streaming", StringComparison.OrdinalIgnoreCase);
    }

    private static bool IsVbCable(DeviceInfo? device)
    {
        if (device is null) return false;
        return device.Name.Contains("CABLE", StringComparison.OrdinalIgnoreCase)
            || device.Name.Contains("VB-Audio", StringComparison.OrdinalIgnoreCase)
            || device.Name.Contains("VB-CABLE", StringComparison.OrdinalIgnoreCase);
    }

    private static bool IsSteamRoute(DeviceInfo? device)
    {
        return device is not null && device.Name.Contains("Steam Streaming", StringComparison.OrdinalIgnoreCase);
    }

    private static string? ValidateRoute(DeviceInfo? capture, DeviceInfo? output)
    {
        if (capture is null) return "choose a capture/source endpoint.";
        if (output is null) return "choose a processed output endpoint.";
        if (capture.Id == output.Id) return "capture/source endpoint and processed output endpoint must be different.";
        if (!IsVirtualSource(capture)) return "capture/source should be a virtual playback endpoint such as VB-CABLE. Steam Streaming Speakers is allowed as a fallback.";
        if (IsVirtualSource(output)) return "processed output should be a real listening device, not another virtual source endpoint.";
        return null;
    }

    private static string RouteRecommendation(DeviceInfo? capture, DeviceInfo? output)
    {
        var error = ValidateRoute(capture, output);
        if (error is not null) return "Needs attention: " + error;
        if (IsVbCable(capture)) return "Recommended v1 route: VB-CABLE source into selected real output.";
        if (IsSteamRoute(capture)) return "Working fallback route: Steam source is usable, but VB-CABLE is the v1 target.";
        return "Route is valid but not recognized as a preferred Axiom source.";
    }

    private string? ValidateLiveProgScript(string path, bool requireUiDefaults = false)
    {
        if (!GetBool("LiveProg", "enabled", true)) return null;
        if (string.IsNullOrWhiteSpace(path)) return "LiveProg is enabled but no EEL file is selected.";
        if (!File.Exists(path)) return "LiveProg file is missing: " + path;

        string text;
        try
        {
            text = File.ReadAllText(path);
        }
        catch (Exception exc)
        {
            return "LiveProg file could not be read: " + exc.Message;
        }

        if (string.IsNullOrWhiteSpace(text)) return "LiveProg file is empty: " + path;
        if (!Regex.IsMatch(text, @"(?m)^@init\b")) return "LiveProg file is missing @init: " + path;
        if (requireUiDefaults && !text.Contains("// UI Defaults", StringComparison.OrdinalIgnoreCase))
        {
            return "test scripts must include a // UI Defaults block in @init.";
        }
        return null;
    }

    private string GetValue(string section, string key, string fallback)
    {
        return config.TryGetValue(section, out var values) && values.TryGetValue(key, out var value) ? value : fallback;
    }

    private void SetValue(string section, string key, string value)
    {
        if (!config.ContainsKey(section)) config[section] = new(StringComparer.OrdinalIgnoreCase);
        config[section][key] = value;
    }

    private static string ConfigKey(string section, string key) => section + "." + key;

    private static (string Section, string Key) SplitConfigKey(string key)
    {
        var split = key.IndexOf('.');
        return split < 0 ? ("", key) : (key[..split], key[(split + 1)..]);
    }

    private bool GetBool(string section, string key, bool fallback)
    {
        var value = GetValue(section, key, fallback ? "true" : "false").ToLowerInvariant();
        return value is "true" or "1" or "yes" or "on";
    }

    private int GetInt(string section, string key, int fallback)
    {
        return int.TryParse(GetValue(section, key, fallback.ToString(CultureInfo.InvariantCulture)), NumberStyles.Any, CultureInfo.InvariantCulture, out var value) ? value : fallback;
    }

    private decimal GetDecimal(string section, string key, decimal fallback)
    {
        return decimal.TryParse(GetValue(section, key, fallback.ToString(CultureInfo.InvariantCulture)), NumberStyles.Any, CultureInfo.InvariantCulture, out var value) ? value : fallback;
    }

    private decimal[] GetArray(string section, string key, int count)
    {
        var values = GetValue(section, key, "").Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
        var result = Enumerable.Repeat(0m, count).ToArray();
        for (var i = 0; i < Math.Min(count, values.Length); i++) result[i] = Decimal(values[i]);
        return result;
    }

    private static decimal Decimal(string value)
    {
        return decimal.TryParse(value, NumberStyles.Any, CultureInfo.InvariantCulture, out var parsed) ? parsed : 0;
    }

    private static int DecimalPlaces(decimal step)
    {
        var bits = decimal.GetBits(step);
        return (bits[3] >> 16) & 0x7F;
    }

    private string BuildRouteStateText(int runningCount, string? routeError, string? scriptError)
    {
        if (routeRecoveryPending) return "Route state: Waiting for device - saved endpoint disconnected";
        if (processorRestartPending) return "Route state: Recovering - " + processorFailureState;
        if (runningCount > 1) return $"Route state: Needs action - {runningCount} processor instances running";
        if (runningCount == 1)
        {
            if (!string.IsNullOrWhiteSpace(processorFailureState)) return "Route state: Recovered - " + processorFailureState;
            return "Route state: Processing - audio is routed through Axiom";
        }
        if (!string.IsNullOrWhiteSpace(processorFailureState)) return "Route state: Needs action - " + processorFailureState;
        if (routeError is not null) return "Route state: Needs action - " + routeError;
        if (scriptError is not null) return "Route state: Needs action - " + scriptError;
        return "Route state: Ready - click Start Processor to hear Axiom";
    }

    private Color RouteStateColor(int runningCount, string? routeError, string? scriptError)
    {
        if (routeRecoveryPending || processorRestartPending || !string.IsNullOrWhiteSpace(processorFailureState) || routeError is not null || scriptError is not null)
        {
            return Color.FromArgb(240, 170, 80);
        }
        return runningCount <= 1 ? Color.FromArgb(73, 217, 151) : Color.FromArgb(240, 120, 120);
    }

    private void SetRouteEvent(string text)
    {
        lastRouteEvent = $"{DateTime.Now:HH:mm:ss} - {text}";
        AppendLog(text);
        UpdateStatus();
    }

    private void UpdateStatus()
    {
        var runningCount = CountRunningProcessors();
        var running = runningCount > 0;
        var capture = captureDevice.SelectedItem as DeviceInfo;
        var output = outputDevice.SelectedItem as DeviceInfo;
        var routeError = ValidateRoute(capture, output);
        var routeConflict = routeError is not null;
        var scriptError = ValidateLiveProgScript(GetValue("LiveProg", "file", ""));
        if (running && processorStartedAt != DateTime.MinValue && (DateTime.Now - processorStartedAt).TotalSeconds > 60)
        {
            unexpectedExitCount = 0;
            unexpectedExitWindowStarted = DateTime.MinValue;
            processorFailureState = "";
        }
        var routeStateText = BuildRouteStateText(runningCount, routeError, scriptError);
        statusLabel.Text = routeStateText;
        statusLabel.ForeColor = RouteStateColor(runningCount, routeError, scriptError);
        routeStateLabel.Text = routeStateText;
        routeStateLabel.ForeColor = statusLabel.ForeColor;
        diagnosticsRouteStateLabel.Text = routeStateText;
        diagnosticsRouteStateLabel.ForeColor = statusLabel.ForeColor;
        routeEventLabel.Text = "Last route event: " + lastRouteEvent;
        routeEventLabel.ForeColor = Color.FromArgb(210, 205, 170);
        diagnosticsRouteEventLabel.Text = routeEventLabel.Text;
        diagnosticsRouteEventLabel.ForeColor = routeEventLabel.ForeColor;
        startButton.Enabled = !running && !routeConflict && scriptError is null && !routeRecoveryPending && !processorRestartPending;
        stopButton.Enabled = running;
        var routeText = $"Route: {FormatDevice(capture)} -> {FormatDevice(output)}. {RouteRecommendation(capture, output)}";
        routeStatusLabel.Text = routeText;
        routeStatusLabel.ForeColor = routeError is null ? Color.FromArgb(73, 217, 151) : Color.FromArgb(240, 170, 80);
        var scriptText = $"Script: {GetValue("LiveProg", "file", "")}. Validation: {scriptError ?? "ok"}";
        scriptStatusLabel.Text = scriptText;
        scriptStatusLabel.ForeColor = scriptError is null ? Color.FromArgb(73, 217, 151) : Color.FromArgb(240, 170, 80);
        diagnosticsRouteLabel.Text = routeText;
        diagnosticsRouteLabel.ForeColor = routeStatusLabel.ForeColor;
        diagnosticsScriptLabel.Text = scriptText;
        diagnosticsScriptLabel.ForeColor = scriptStatusLabel.ForeColor;
        diagnosticsProcessorLabel.Text = $"Processor: {(running ? $"{runningCount} running" : "stopped")}; route recovery {(routeRecoveryPending ? "waiting" : "ready")}; crash recovery {(processorRestartPending ? $"{unexpectedExitCount}/3 pending" : "ready")}; buffer {controllerState.BufferMs} ms; command {lastProcessorCommand}";
        diagnosticsProfileLabel.Text = $"Profile: {lastProfileName}";
        if (running && lastAudioPackets == 0 && processorStartedAt != DateTime.MinValue && (DateTime.Now - processorStartedAt).TotalSeconds > 6)
        {
            audioHealthLabel.Text = "No audio packets detected. Start playback or route the player/browser to the capture/source endpoint.";
            audioHealthLabel.ForeColor = Color.FromArgb(240, 170, 80);
            lastAudioHealth = audioHealthLabel.Text;
        }
        RefreshSetupStatus();
    }

    private void AppendLog(string? line)
    {
        if (string.IsNullOrWhiteSpace(line)) return;
        if (InvokeRequired)
        {
            BeginInvoke(() => AppendLog(line));
            return;
        }
        UpdateProcessorHealth(line);
        logBox.AppendText(line + Environment.NewLine);
    }

    private void UpdateProcessorHealth(string line)
    {
        var captureMatch = Regex.Match(line, @"^\[INFO\]\s+Capture format:\s+(?<format>.+)$");
        if (captureMatch.Success)
        {
            lastCaptureFormat = captureMatch.Groups["format"].Value;
            captureFormatLabel.Text = "Capture: " + lastCaptureFormat;
            return;
        }

        var renderMatch = Regex.Match(line, @"^\[INFO\]\s+Render format:\s+(?<format>.+)$");
        if (renderMatch.Success)
        {
            lastRenderFormat = renderMatch.Groups["format"].Value;
            renderFormatLabel.Text = "Render: " + lastRenderFormat;
            return;
        }

        var bufferMatch = Regex.Match(line, @"^\[INFO\]\s+Buffer target:\s+(?<buffer>\d+)\s+ms");
        if (bufferMatch.Success)
        {
            lastBufferStatus = bufferMatch.Groups["buffer"].Value + " ms active";
            bufferLabel.Text = "Buffer: " + lastBufferStatus;
            return;
        }

        var audioMatch = Regex.Match(line, @"^\[AUDIO\]\s+frames=(?<frames>\d+)\s+dropped=(?<dropped>\d+)\s+silent=(?<silent>\d+)\s+packets=(?<packets>\d+)\s+conversionErrors=(?<errors>\d+)");
        if (audioMatch.Success)
        {
            var dropped = long.Parse(audioMatch.Groups["dropped"].Value, CultureInfo.InvariantCulture);
            var errors = long.Parse(audioMatch.Groups["errors"].Value, CultureInfo.InvariantCulture);
            lastAudioPackets = long.Parse(audioMatch.Groups["packets"].Value, CultureInfo.InvariantCulture);
            lastAudioHealth =
                $"Frames: {audioMatch.Groups["frames"].Value} | " +
                $"Dropped: {audioMatch.Groups["dropped"].Value} | " +
                $"Silent: {audioMatch.Groups["silent"].Value} | " +
                $"Packets: {audioMatch.Groups["packets"].Value} | " +
                $"Conversion errors: {audioMatch.Groups["errors"].Value}";
            audioHealthLabel.Text = lastAudioHealth;
            audioHealthLabel.ForeColor = dropped == 0 && errors == 0
                ? Color.FromArgb(73, 217, 151)
                : Color.FromArgb(240, 170, 80);

            var performanceMatch = Regex.Match(
                line,
                @"\s+discontinuities=(?<discontinuities>\d+)\s+renderStarvations=(?<starvations>\d+)\s+renderErrors=(?<renderErrors>\d+)\s+dspAvgUs=(?<dspAvg>\d+)\s+dspMaxUs=(?<dspMax>\d+)\s+dspCalls=(?<dspCalls>\d+)\s+dspDeadlineMisses=(?<deadlineMisses>\d+)\s+dspCriticalStalls=(?<criticalStalls>\d+)\s+paddingMin=(?<paddingMin>\d+)\s+paddingMax=(?<paddingMax>\d+)\s+bufferFrames=(?<bufferFrames>\d+)");
            if (performanceMatch.Success)
            {
                var discontinuities = long.Parse(performanceMatch.Groups["discontinuities"].Value, CultureInfo.InvariantCulture);
                var starvations = long.Parse(performanceMatch.Groups["starvations"].Value, CultureInfo.InvariantCulture);
                var renderErrors = long.Parse(performanceMatch.Groups["renderErrors"].Value, CultureInfo.InvariantCulture);
                lastPerformanceHealth =
                    $"Discontinuities: {performanceMatch.Groups["discontinuities"].Value} | " +
                    $"Render starvations: {performanceMatch.Groups["starvations"].Value} | " +
                    $"Render errors: {performanceMatch.Groups["renderErrors"].Value} | " +
                    $"DSP avg/max: {performanceMatch.Groups["dspAvg"].Value}/{performanceMatch.Groups["dspMax"].Value} us | " +
                    $"Deadline misses: {performanceMatch.Groups["deadlineMisses"].Value} | " +
                    $"Critical stalls: {performanceMatch.Groups["criticalStalls"].Value} | " +
                    $"Padding: {performanceMatch.Groups["paddingMin"].Value}-{performanceMatch.Groups["paddingMax"].Value}/{performanceMatch.Groups["bufferFrames"].Value} frames";
                performanceHealthLabel.Text = lastPerformanceHealth;
                performanceHealthLabel.ForeColor = discontinuities == 0 && starvations == 0 && renderErrors == 0
                    ? Color.FromArgb(73, 217, 151)
                    : Color.FromArgb(240, 170, 80);

                RecordHealthSample(new HealthSample(
                    DateTime.UtcNow,
                    long.Parse(audioMatch.Groups["frames"].Value, CultureInfo.InvariantCulture),
                    dropped,
                    long.Parse(audioMatch.Groups["silent"].Value, CultureInfo.InvariantCulture),
                    lastAudioPackets,
                    errors,
                    discontinuities,
                    starvations,
                    renderErrors,
                    long.Parse(performanceMatch.Groups["dspAvg"].Value, CultureInfo.InvariantCulture),
                    long.Parse(performanceMatch.Groups["dspMax"].Value, CultureInfo.InvariantCulture),
                    long.Parse(performanceMatch.Groups["dspCalls"].Value, CultureInfo.InvariantCulture),
                    long.Parse(performanceMatch.Groups["deadlineMisses"].Value, CultureInfo.InvariantCulture),
                    long.Parse(performanceMatch.Groups["criticalStalls"].Value, CultureInfo.InvariantCulture),
                    long.Parse(performanceMatch.Groups["paddingMin"].Value, CultureInfo.InvariantCulture),
                    long.Parse(performanceMatch.Groups["paddingMax"].Value, CultureInfo.InvariantCulture),
                    long.Parse(performanceMatch.Groups["bufferFrames"].Value, CultureInfo.InvariantCulture),
                    controllerState.CaptureId,
                    controllerState.OutputId,
                    controllerState.BufferMs,
                    lastProfileName));
            }
        }
    }

    private static string Quote(string value) => '"' + value + '"';

    private TabPage NewPage(string title) => new(title) { BackColor = BackColor, ForeColor = ForeColor };

    private FlowLayoutPanel NewStack() => new()
    {
        Dock = DockStyle.Fill,
        FlowDirection = FlowDirection.TopDown,
        WrapContents = false,
        AutoScroll = true,
        Padding = new Padding(14),
        BackColor = BackColor
    };

    private FlowLayoutPanel Row() => new()
    {
        AutoSize = true,
        WrapContents = false,
        FlowDirection = FlowDirection.LeftToRight,
        Padding = new Padding(0, 4, 0, 4),
        BackColor = BackColor
    };

    private Label Label(string text) => new()
    {
        Text = text,
        Width = 230,
        AutoSize = false,
        TextAlign = ContentAlignment.MiddleLeft,
        ForeColor = ForeColor,
        Padding = new Padding(4)
    };

    private Label HealthLabel(string text) => new()
    {
        Text = text,
        AutoSize = true,
        ForeColor = Color.FromArgb(210, 225, 215),
        Font = new Font("Consolas", 9),
        Padding = new Padding(4)
    };

    private Button Button(string text, EventHandler handler)
    {
        var button = new Button { Text = text, AutoSize = true, Padding = new Padding(8, 3, 8, 3) };
        button.Click += handler;
        return button;
    }
}
