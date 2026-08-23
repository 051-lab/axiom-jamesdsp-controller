param(
    [ValidateSet("Start", "Mark", "Finish", "Monitor")]
    [string]$Action = "Start",
    [string]$Label = "",
    [string]$SessionRoot = "",
    [int]$IntervalSeconds = 2
)

$ErrorActionPreference = "Stop"
$tests = Split-Path -Parent $MyInvocation.MyCommand.Path
$harness = Split-Path -Parent $tests
$newApplicationRoot = Join-Path $env:ProgramFiles "JamesDSP Controller"
$legacyApplicationRoot = Join-Path $env:ProgramFiles "Axiom JamesDSP Controller"
$applicationRoot = if (Test-Path (Join-Path $newApplicationRoot "JamesDSPController.exe")) {
    $newApplicationRoot
} elseif (Test-Path (Join-Path $legacyApplicationRoot "JamesDSPController.exe")) {
    $legacyApplicationRoot
} else {
    $newApplicationRoot
}
$consoleExe = Join-Path $applicationRoot "JamesDSPConsole.exe"
$dataRoot = Join-Path $env:LOCALAPPDATA "JamesDSP\Controller"
$healthPath = Join-Path $dataRoot "diagnostics\health-history.jsonl"
$qualificationRoot = Join-Path $env:LOCALAPPDATA "JamesDSP\ManualRecovery"
$activeSessionPath = Join-Path $qualificationRoot "active-session.json"

function Read-JsonFile([string]$Path) {
    return Get-Content $Path -Raw | ConvertFrom-Json
}

function Get-ActiveSession {
    if (-not (Test-Path $activeSessionPath)) {
        throw "No active manual recovery session was found."
    }
    return Read-JsonFile $activeSessionPath
}

function Get-LatestHealth {
    if (-not (Test-Path $healthPath)) { return $null }
    $stream = [IO.File]::Open($healthPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        $reader = [IO.StreamReader]::new($stream)
        try {
            $last = $null
            while (-not $reader.EndOfStream) {
                $line = $reader.ReadLine()
                if (-not [string]::IsNullOrWhiteSpace($line)) { $last = $line }
            }
        }
        finally {
            $reader.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
    if (-not $last) { return $null }
    try { return $last | ConvertFrom-Json } catch { return $null }
}

function Get-Snapshot {
    $devices = @()
    $default = $null
    if (Test-Path $consoleExe) {
        try { $devices = @((& $consoleExe --list-devices-json | ConvertFrom-Json).devices) } catch {}
        try { $default = & $consoleExe --get-default-json | ConvertFrom-Json } catch {}
    }
    $power = Get-CimInstance -Namespace root\wmi -ClassName BatteryStatus -ErrorAction SilentlyContinue |
        Select-Object -First 1
    $processors = @(
        Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -in @("JamesDSPController.exe", "JamesDSPConsole.exe", "AxiomJamesDSPController.exe", "AxiomJamesDSPConsole.exe") } |
            Select-Object ProcessId, ParentProcessId, Name, ExecutablePath, CreationDate
    )
    return [ordered]@{
        timestampUtc = (Get-Date).ToUniversalTime().ToString("O")
        devices = $devices
        defaultRoute = $default
        processes = $processors
        power = [ordered]@{
            online = if ($power) { [bool]$power.PowerOnline } else { $null }
            charging = if ($power) { [bool]$power.Charging } else { $null }
            discharging = if ($power) { [bool]$power.Discharging } else { $null }
        }
        health = Get-LatestHealth
    }
}

if ($Action -eq "Monitor") {
    if ([string]::IsNullOrWhiteSpace($SessionRoot)) { throw "SessionRoot is required for Monitor." }
    $timelinePath = Join-Path $SessionRoot "timeline.jsonl"
    $stopPath = Join-Path $SessionRoot "stop.request"
    while (-not (Test-Path $stopPath)) {
        (Get-Snapshot | ConvertTo-Json -Depth 8 -Compress) |
            Add-Content $timelinePath -Encoding UTF8
        Start-Sleep -Seconds ([Math]::Max(1, $IntervalSeconds))
    }
    exit 0
}

if ($Action -eq "Start") {
    if (Test-Path $activeSessionPath) {
        $active = Read-JsonFile $activeSessionPath
        $existing = Get-Process -Id $active.monitorPid -ErrorAction SilentlyContinue
        if ($existing) { throw "A manual recovery session is already active at $($active.sessionRoot)." }
        Remove-Item $activeSessionPath -Force
    }

    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $SessionRoot = Join-Path $qualificationRoot $stamp
    New-Item $SessionRoot -ItemType Directory -Force | Out-Null
    $baseline = Get-Snapshot
    $baseline | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $SessionRoot "baseline.json") -Encoding UTF8
    [ordered]@{
        timestampUtc = (Get-Date).ToUniversalTime().ToString("O")
        label = "session-start"
    } | ConvertTo-Json -Compress | Add-Content (Join-Path $SessionRoot "events.jsonl") -Encoding UTF8

    $monitor = Start-Process powershell.exe -WindowStyle Hidden -PassThru -ArgumentList @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $MyInvocation.MyCommand.Path,
        "-Action", "Monitor",
        "-SessionRoot", $SessionRoot,
        "-IntervalSeconds", $IntervalSeconds
    )
    $active = [ordered]@{
        sessionRoot = $SessionRoot
        monitorPid = $monitor.Id
        startedAtUtc = (Get-Date).ToUniversalTime().ToString("O")
    }
    New-Item $qualificationRoot -ItemType Directory -Force | Out-Null
    $active | ConvertTo-Json | Set-Content $activeSessionPath -Encoding UTF8
    Write-Host "Manual recovery session started: $SessionRoot"
    Write-Host "Monitor PID: $($monitor.Id)"
    exit 0
}

$active = Get-ActiveSession
$SessionRoot = [string]$active.sessionRoot

if ($Action -eq "Mark") {
    if ([string]::IsNullOrWhiteSpace($Label)) { throw "Label is required for Mark." }
    [ordered]@{
        timestampUtc = (Get-Date).ToUniversalTime().ToString("O")
        label = $Label
    } | ConvertTo-Json -Compress | Add-Content (Join-Path $SessionRoot "events.jsonl") -Encoding UTF8
    Write-Host "Marked: $Label"
    exit 0
}

New-Item (Join-Path $SessionRoot "stop.request") -ItemType File -Force | Out-Null
$monitorProcess = Get-Process -Id $active.monitorPid -ErrorAction SilentlyContinue
if ($monitorProcess) {
    $monitorProcess.WaitForExit(10000)
    if (-not $monitorProcess.HasExited) { Stop-Process -Id $monitorProcess.Id -Force }
}
$final = Get-Snapshot
$final | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $SessionRoot "final.json") -Encoding UTF8
[ordered]@{
    timestampUtc = (Get-Date).ToUniversalTime().ToString("O")
    label = "session-finish"
} | ConvertTo-Json -Compress | Add-Content (Join-Path $SessionRoot "events.jsonl") -Encoding UTF8

$timeline = @(
    Get-Content (Join-Path $SessionRoot "timeline.jsonl") -ErrorAction SilentlyContinue |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        ForEach-Object { $_ | ConvertFrom-Json }
)
$events = @(
    Get-Content (Join-Path $SessionRoot "events.jsonl") |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        ForEach-Object { $_ | ConvertFrom-Json }
)
$processorCounts = @($timeline | ForEach-Object { @($_.processes | Where-Object Name -eq "JamesDSPConsole.exe").Count })
$deviceFingerprints = @(
    $timeline |
        ForEach-Object { (@($_.devices | Sort-Object id | ForEach-Object id) -join "|") } |
        Select-Object -Unique
)
$report = [ordered]@{
    schemaVersion = 1
    result = "review_required"
    sessionRoot = $SessionRoot
    startedAtUtc = $active.startedAtUtc
    endedAtUtc = (Get-Date).ToUniversalTime().ToString("O")
    samples = $timeline.Count
    events = $events
    observedDeviceStates = $deviceFingerprints.Count
    processorCountMinimum = if ($processorCounts.Count) { ($processorCounts | Measure-Object -Minimum).Minimum } else { 0 }
    processorCountMaximum = if ($processorCounts.Count) { ($processorCounts | Measure-Object -Maximum).Maximum } else { 0 }
    baseline = Read-JsonFile (Join-Path $SessionRoot "baseline.json")
    final = $final
}
$report | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $SessionRoot "manual-recovery-report.json") -Encoding UTF8

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("# JamesDSP Controller Manual Recovery Qualification")
$lines.Add("")
$lines.Add("- Result: **REVIEW REQUIRED**")
$lines.Add("- Started: $($active.startedAtUtc)")
$lines.Add("- Ended: $($report.endedAtUtc)")
$lines.Add("- Timeline samples: $($timeline.Count)")
$lines.Add("- Distinct device states: $($deviceFingerprints.Count)")
$lines.Add("- Native processor count range: $($report.processorCountMinimum)-$($report.processorCountMaximum)")
$lines.Add("")
$lines.Add("## Events")
$lines.Add("")
foreach ($event in $events) {
    $lines.Add("- $($event.timestampUtc): $($event.label)")
}
$lines.Add("")
$lines.Add('Review `timeline.jsonl`, `baseline.json`, and `final.json` before marking the qualification pass or fail.')
$lines | Set-Content (Join-Path $SessionRoot "manual-recovery-report.md") -Encoding UTF8
Remove-Item $activeSessionPath -Force
Write-Host "Manual recovery session finished: $SessionRoot"
Write-Host "Report: $(Join-Path $SessionRoot 'manual-recovery-report.md')"
