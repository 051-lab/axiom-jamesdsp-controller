param(
    [double]$DurationMinutes = 60,
    [int]$CrashCount = 2,
    [int]$ConfigReloadCount = 2,
    [int]$WarmupSeconds = 12,
    [string]$ApplicationRoot = "",
    [string]$StateSeedPath = "",
    [switch]$SkipAudioProbe,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$harness = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$package = if ([string]::IsNullOrWhiteSpace($ApplicationRoot)) {
    Join-Path $harness "dist\JamesDSPController-win-x64"
} else {
    [IO.Path]::GetFullPath($ApplicationRoot)
}
$controllerExe = Join-Path $package "JamesDSPController.exe"
$consoleExe = Join-Path $package "JamesDSPConsole.exe"
$runStamp = Get-Date -Format "yyyyMMdd-HHmmss"
$runRoot = Join-Path $env:LOCALAPPDATA "JamesDSP\SoakTests\$runStamp"
$dataRoot = Join-Path $runRoot "data"
$reportJson = Join-Path $runRoot "soak-report.json"
$reportMarkdown = Join-Path $runRoot "soak-report.md"
$probeLogPath = Join-Path $runRoot "soak-probe.log"
$historyPath = Join-Path $dataRoot "diagnostics\health-history.jsonl"
$configPath = Join-Path $dataRoot "jamesdsp-controller.ini"
$originalDefault = $null
$controllerProcess = $null
$probeProcess = $null
$capture = $null
$output = $null
$powerStateAtStart = $null
$previousDataRoot = $env:JAMESDSP_DATA_ROOT
$observations = [System.Collections.Generic.List[object]]::new()
$recoveryEvents = [System.Collections.Generic.List[object]]::new()
$reloadEvents = [System.Collections.Generic.List[object]]::new()
$healthReadWarnings = [System.Collections.Generic.List[object]]::new()
$lastHealthSamples = @()
$startedAt = Get-Date

function Get-PowerSourceState {
    $battery = Get-CimInstance -Namespace root\wmi -ClassName BatteryStatus -ErrorAction SilentlyContinue |
        Select-Object -First 1
    return [ordered]@{
        powerOnline = if ($battery) { [bool]$battery.PowerOnline } else { $null }
        charging = if ($battery) { [bool]$battery.Charging } else { $null }
        discharging = if ($battery) { [bool]$battery.Discharging } else { $null }
        remainingCapacity = if ($battery) { $battery.RemainingCapacity } else { $null }
    }
}

function Get-PowerSourceChanges([datetime]$Since) {
    return @(
        Get-WinEvent -FilterHashtable @{
            LogName = "System"
            ProviderName = "Microsoft-Windows-Kernel-Power"
            Id = 105
            StartTime = $Since
        } -ErrorAction SilentlyContinue |
            ForEach-Object {
                [ordered]@{
                    timestamp = $_.TimeCreated.ToUniversalTime().ToString("O")
                    message = $_.Message
                }
            }
    )
}

function Stop-AxiomProcesses {
    Get-Process JamesDSPController,JamesDSPConsole,AxiomJamesDSPController,AxiomJamesDSPConsole -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
}

function Find-AxiomButton([string]$Name) {
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    $root = [System.Windows.Automation.AutomationElement]::RootElement
    $windowCondition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::NameProperty,
        "JamesDSP Controller")
    $window = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $windowCondition)
    if (-not $window) { return $null }
    $buttonCondition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::NameProperty,
        $Name)
    return $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $buttonCondition)
}

function Wait-ForPath([string]$Path, [int]$TimeoutSeconds = 15) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path $Path) { return $true }
        Start-Sleep -Milliseconds 250
    }
    return $false
}

function Wait-ForProcessor([int]$PreviousPid = 0, [int]$TimeoutSeconds = 15) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $candidate = Get-Process JamesDSPConsole -ErrorAction SilentlyContinue |
            Where-Object { $_.Id -ne $PreviousPid } |
            Select-Object -First 1
        if ($candidate) { return $candidate }
        Start-Sleep -Milliseconds 250
    }
    return $null
}

function Add-HealthReadWarning([string]$Stage, [string]$Message) {
    $healthReadWarnings.Add([ordered]@{
        timestamp = (Get-Date).ToUniversalTime().ToString("O")
        stage = $Stage
        message = $Message
    })
}

function Read-HealthHistoryText {
    if (-not (Test-Path $historyPath)) { return $null }

    for ($attempt = 0; $attempt -lt 10; $attempt += 1) {
        try {
            $stream = [IO.File]::Open(
                $historyPath,
                [IO.FileMode]::Open,
                [IO.FileAccess]::Read,
                [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
            try {
                $memory = [IO.MemoryStream]::new()
                try {
                    $stream.CopyTo($memory)
                    return [Text.Encoding]::UTF8.GetString($memory.ToArray())
                }
                finally {
                    $memory.Dispose()
                }
            }
            finally {
                $stream.Dispose()
            }
        }
        catch {
            if ($attempt -eq 9) {
                Add-HealthReadWarning "snapshot" $_.Exception.Message
                return $null
            }
            Start-Sleep -Milliseconds ([Math]::Min(500, 25 * ($attempt + 1)))
        }
    }

    return $null
}

function Read-HealthSamples {
    if (-not (Test-Path $historyPath)) { return @() }

    $text = Read-HealthHistoryText
    if ($null -eq $text) {
        return @($lastHealthSamples)
    }

    $lines = @($text -split "\r?\n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    $samples = [System.Collections.Generic.List[object]]::new()
    $parseFailures = 0
    for ($index = 0; $index -lt $lines.Count; $index += 1) {
        try {
            $samples.Add(($lines[$index] | ConvertFrom-Json -ErrorAction Stop))
        }
        catch {
            $parseFailures += 1
            if ($index -ne $lines.Count - 1) {
                Add-HealthReadWarning "parse" "Skipped malformed health-history line $($index + 1): $($_.Exception.Message)"
            }
        }
    }
    if ($parseFailures -gt 0) {
        Add-HealthReadWarning "parse" "Skipped $parseFailures malformed health-history line(s)."
    }
    if ($samples.Count -gt 0) {
        $script:lastHealthSamples = @($samples)
    }
    return @($samples)
}

function Sum-CounterAcrossResets([object[]]$Samples, [string]$Property) {
    if ($Samples.Count -eq 0) { return [long]0 }
    [long]$total = 0
    [long]$previous = 0
    foreach ($sample in $Samples) {
        [long]$current = $sample.$Property
        if ($current -ge $previous) {
            $total += $current - $previous
        } else {
            $total += $current
        }
        $previous = $current
    }
    return $total
}

function Add-Gate([System.Collections.Generic.List[object]]$Gates, [string]$Name, [bool]$Passed, [string]$Detail) {
    $Gates.Add([ordered]@{
        name = $Name
        passed = $Passed
        detail = $Detail
    })
}

function New-SoakMetrics([object[]]$Samples) {
    $processedFrames = Sum-CounterAcrossResets $Samples "Frames"
    $packets = Sum-CounterAcrossResets $Samples "Packets"
    $dropped = Sum-CounterAcrossResets $Samples "Dropped"
    $conversionErrors = Sum-CounterAcrossResets $Samples "ConversionErrors"
    $discontinuities = Sum-CounterAcrossResets $Samples "Discontinuities"
    $renderStarvations = Sum-CounterAcrossResets $Samples "RenderStarvations"
    $renderErrors = Sum-CounterAcrossResets $Samples "RenderErrors"
    $dspCalls = Sum-CounterAcrossResets $Samples "DspCalls"
    $dspDeadlineMisses = Sum-CounterAcrossResets $Samples "DspDeadlineMisses"
    $dspCriticalStalls = Sum-CounterAcrossResets $Samples "DspCriticalStalls"
    $deadlineMissRate = if ($dspCalls -gt 0) { $dspDeadlineMisses / [double]$dspCalls } else { 1.0 }
    $maximumDspUs = if ($Samples.Count -gt 0) { ($Samples | Measure-Object DspMaximumUs -Maximum).Maximum } else { 0 }
    $maximumPadding = if ($Samples.Count -gt 0) { ($Samples | Measure-Object PaddingMaximum -Maximum).Maximum } else { 0 }
    $bufferFrames = if ($Samples.Count -gt 0) { ($Samples | Measure-Object BufferFrames -Maximum).Maximum } else { 0 }
    $maximumPaddingRate = if ($bufferFrames -gt 0) { $maximumPadding / [double]$bufferFrames } else { 1.0 }

    return [ordered]@{
        healthSamples = $Samples.Count
        processedFrames = $processedFrames
        packets = $packets
        droppedFrames = $dropped
        conversionErrors = $conversionErrors
        discontinuities = $discontinuities
        renderStarvations = $renderStarvations
        renderErrors = $renderErrors
        dspCalls = $dspCalls
        dspDeadlineMisses = $dspDeadlineMisses
        dspDeadlineMissRate = $deadlineMissRate
        dspCriticalStalls = $dspCriticalStalls
        maximumDspUs = $maximumDspUs
        maximumRenderPadding = $maximumPadding
        renderBufferFrames = $bufferFrames
        maximumRenderPaddingRate = $maximumPaddingRate
        crashRecoveries = @($recoveryEvents | Where-Object { $_.recovered }).Count
        configReloads = @($reloadEvents | Where-Object { $_.survived }).Count
    }
}

function New-SoakGates([object]$Metrics) {
    $gates = [System.Collections.Generic.List[object]]::new()
    $controllerAlive = $null -ne (Get-Process JamesDSPController -ErrorAction SilentlyContinue)
    $processorAlive = @(Get-LiveProcessorProcesses).Count -eq 1

    Add-Gate $gates "controller remained running" $controllerAlive "alive=$controllerAlive"
    Add-Gate $gates "processor remained running" $processorAlive "alive=$processorAlive"
    Add-Gate $gates "health telemetry recorded" ($Metrics.healthSamples -ge 2) "samples=$($Metrics.healthSamples)"
    Add-Gate $gates "audio frames processed" ($Metrics.processedFrames -gt 0) "frames=$($Metrics.processedFrames)"
    Add-Gate $gates "audio packets observed" ($Metrics.packets -gt 0) "packets=$($Metrics.packets)"
    Add-Gate $gates "planned crashes recovered" ($Metrics.crashRecoveries -eq $CrashCount) "recoveries=$($Metrics.crashRecoveries)/$CrashCount"
    Add-Gate $gates "config reloads survived" ($Metrics.configReloads -eq $ConfigReloadCount) "reloads=$($Metrics.configReloads)/$ConfigReloadCount"
    Add-Gate $gates "no dropped frames" ($Metrics.droppedFrames -eq 0) "dropped=$($Metrics.droppedFrames)"
    Add-Gate $gates "no conversion errors" ($Metrics.conversionErrors -eq 0) "errors=$($Metrics.conversionErrors)"
    Add-Gate $gates "no render errors" ($Metrics.renderErrors -eq 0) "errors=$($Metrics.renderErrors)"
    Add-Gate $gates "no render starvations" ($Metrics.renderStarvations -eq 0) "starvations=$($Metrics.renderStarvations)"
    Add-Gate $gates "capture discontinuities caused no audio loss" ($Metrics.droppedFrames -eq 0 -and $Metrics.renderStarvations -eq 0) "discontinuities=$($Metrics.discontinuities); dropped=$($Metrics.droppedFrames); starvations=$($Metrics.renderStarvations)"
    Add-Gate $gates "no buffer-budget DSP stalls" ($Metrics.dspCriticalStalls -eq 0) "criticalStalls=$($Metrics.dspCriticalStalls)"
    Add-Gate $gates "bounded DSP deadline pressure" ($Metrics.dspDeadlineMissRate -le 0.10) "misses=$($Metrics.dspDeadlineMisses)/$($Metrics.dspCalls); rate=$($Metrics.dspDeadlineMissRate.ToString("P4")); limit=10%"
    Add-Gate $gates "render buffer retains headroom" ($Metrics.maximumRenderPaddingRate -le 0.90) "maximum=$($Metrics.maximumRenderPadding)/$($Metrics.renderBufferFrames); usage=$($Metrics.maximumRenderPaddingRate.ToString("P2")); limit=90%"
    return $gates
}

function Get-HealthAnalysis([object[]]$Samples) {
    $dropEvents = [System.Collections.Generic.List[object]]::new()
    $discontinuityEvents = [System.Collections.Generic.List[object]]::new()
    $deadlineEvents = [System.Collections.Generic.List[object]]::new()
    $previous = $null
    foreach ($sample in $Samples) {
        if ($previous) {
            [long]$dropDelta = $sample.Dropped - $previous.Dropped
            [long]$discontinuityDelta = $sample.Discontinuities - $previous.Discontinuities
            [long]$deadlineDelta = $sample.DspDeadlineMisses - $previous.DspDeadlineMisses
            if ($dropDelta -gt 0) {
                $dropEvents.Add([ordered]@{
                    timestamp = $sample.TimestampUtc
                    dropDelta = $dropDelta
                    dropped = $sample.Dropped
                    discontinuities = $sample.Discontinuities
                    deadlineMisses = $sample.DspDeadlineMisses
                    paddingMaximum = $sample.PaddingMaximum
                    frames = $sample.Frames
                    packets = $sample.Packets
                })
            }
            if ($discontinuityDelta -gt 0) {
                $discontinuityEvents.Add([ordered]@{
                    timestamp = $sample.TimestampUtc
                    discontinuityDelta = $discontinuityDelta
                    discontinuities = $sample.Discontinuities
                    dropped = $sample.Dropped
                    paddingMaximum = $sample.PaddingMaximum
                })
            }
            if ($deadlineDelta -gt 0) {
                $deadlineEvents.Add([ordered]@{
                    timestamp = $sample.TimestampUtc
                    deadlineMissDelta = $deadlineDelta
                    deadlineMisses = $sample.DspDeadlineMisses
                    dropped = $sample.Dropped
                    paddingMaximum = $sample.PaddingMaximum
                    maximumDspUs = $sample.DspMaximumUs
                })
            }
        }
        $previous = $sample
    }
    return [ordered]@{
        sampleCount = $Samples.Count
        firstTimestampUtc = if ($Samples.Count -gt 0) { $Samples[0].TimestampUtc } else { $null }
        lastTimestampUtc = if ($Samples.Count -gt 0) { $Samples[$Samples.Count - 1].TimestampUtc } else { $null }
        dropEvents = @($dropEvents)
        discontinuityEvents = @($discontinuityEvents)
        deadlineEvents = @($deadlineEvents)
    }
}

function Get-EventWindowSummaries([object]$HealthAnalysis, [datetime]$EndedAt) {
    $windows = [System.Collections.Generic.List[object]]::new()
    foreach ($event in @($HealthAnalysis.dropEvents)) {
        $time = [datetime]$event.timestamp
        $windows.Add([ordered]@{ reason = "drop"; start = $time.AddMinutes(-5); end = $time.AddMinutes(5) })
    }
    foreach ($event in @($HealthAnalysis.discontinuityEvents)) {
        $time = [datetime]$event.timestamp
        $windows.Add([ordered]@{ reason = "discontinuity"; start = $time.AddMinutes(-2); end = $time.AddMinutes(2) })
    }
    foreach ($event in @($HealthAnalysis.deadlineEvents)) {
        $time = [datetime]$event.timestamp
        $windows.Add([ordered]@{ reason = "deadline"; start = $time.AddMinutes(-2); end = $time.AddMinutes(2) })
    }
    $windows.Add([ordered]@{ reason = "run-end"; start = $EndedAt.AddMinutes(-5); end = $EndedAt.AddMinutes(5) })

    $summaries = [System.Collections.Generic.List[object]]::new()
    foreach ($window in $windows) {
        $events = @(
            foreach ($log in @("System", "Application")) {
                Get-WinEvent -FilterHashtable @{
                    LogName = $log
                    StartTime = $window.start
                    EndTime = $window.end
                } -ErrorAction SilentlyContinue |
                    Where-Object {
                        $_.ProviderName -match "Hyper-V-VmSwitch|BTHUSB|Windows Error Reporting|Kernel-Power|HP Comm Recovery|WindowsUpdate|Display|WHEA|Power" -or
                        $_.LevelDisplayName -in @("Warning", "Error", "Critical")
                    } |
                    Select-Object -First 25 |
                    ForEach-Object {
                        [ordered]@{
                            timestamp = $_.TimeCreated.ToUniversalTime().ToString("O")
                            log = $log
                            provider = $_.ProviderName
                            id = $_.Id
                            level = $_.LevelDisplayName
                            message = ($_.Message -replace "\s+", " ").Trim()
                        }
                    }
            }
        )
        $summaries.Add([ordered]@{
            reason = $window.reason
            startUtc = $window.start.ToUniversalTime().ToString("O")
            endUtc = $window.end.ToUniversalTime().ToString("O")
            count = $events.Count
            events = @($events)
        })
    }
    return @($summaries)
}

function New-SoakClassification([string]$SourceResult, [object]$Metrics, [object[]]$Gates, [datetime]$EndedAt, [string]$ErrorMessage = "") {
    $durationMinutes = ($EndedAt - $startedAt).TotalMinutes
    $completedFullDuration = $durationMinutes -ge ([double]$DurationMinutes - 0.05)
    $audioIntegrityPassed = @($Gates | Where-Object { -not $_.passed }).Count -eq 0
    $harnessError = -not [string]::IsNullOrWhiteSpace($ErrorMessage)
    $failureCategory = "none"
    $evidenceDecision = "pass"

    if ($harnessError) {
        $failureCategory = "harness_error"
        $evidenceDecision = "investigate"
    } elseif (-not $completedFullDuration) {
        $failureCategory = "incomplete_duration"
        $evidenceDecision = "investigate"
    } elseif (-not $audioIntegrityPassed) {
        $failureCategory = "audio_integrity_failure"
        $evidenceDecision = "fail"
    } elseif ($SourceResult -eq "pass_with_environment_warning") {
        $failureCategory = "environment_warning"
        $evidenceDecision = "pass_with_environment_warning"
    }

    return [ordered]@{
        evidenceDecision = $evidenceDecision
        failureCategory = $failureCategory
        completedFullDuration = $completedFullDuration
        completedDurationMinutes = [Math]::Round($durationMinutes, 3)
        requestedDurationMinutes = $DurationMinutes
        audioIntegrityPassed = $audioIntegrityPassed
        harnessError = $harnessError
        error = $ErrorMessage
    }
}

function Get-ProcessorProcessDetails {
    return @(
        Get-CimInstance Win32_Process |
            Where-Object { $_.Name -in @("JamesDSPController.exe", "JamesDSPConsole.exe", "AxiomJamesDSPController.exe", "AxiomJamesDSPConsole.exe") } |
            Select-Object ProcessId, ParentProcessId, Name, ExecutablePath, CommandLine, CreationDate
    )
}

function Get-LiveProcessorProcesses {
    $ids = @(
        Get-CimInstance Win32_Process |
            Where-Object {
                $_.Name -eq "JamesDSPConsole.exe" -and
                $_.CommandLine -match "(^|\s)--watch-config(\s|$)"
            } |
            ForEach-Object { [int]$_.ProcessId }
    )
    return @(
        $ids |
            ForEach-Object { Get-Process -Id $_ -ErrorAction SilentlyContinue }
    )
}

function Write-Reports([object]$Report) {
    New-Item $runRoot -ItemType Directory -Force | Out-Null
    $Report | ConvertTo-Json -Depth 8 | Set-Content $reportJson -Encoding UTF8

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("# JamesDSP Controller Windows Soak Report")
    $lines.Add("")
    $lines.Add("- Result: **$($Report.result.ToUpperInvariant())**")
    $lines.Add("- Started: $($Report.startedAt)")
    $lines.Add("- Ended: $($Report.endedAt)")
    $lines.Add("- Requested duration: $($Report.requestedDurationMinutes) minutes")
    if ($Report.classification) {
        $lines.Add("- Evidence decision: $($Report.classification.evidenceDecision)")
        $lines.Add("- Failure category: $($Report.classification.failureCategory)")
        $lines.Add("- Completed duration: $($Report.classification.completedDurationMinutes) minutes")
    }
    if ($Report.metrics) {
        $lines.Add("- Health samples: $($Report.metrics.healthSamples)")
        $lines.Add("- Processed frames: $($Report.metrics.processedFrames)")
        $lines.Add("- Packets: $($Report.metrics.packets)")
        $lines.Add("- Crash recoveries: $($Report.metrics.crashRecoveries)/$($Report.requestedCrashCount)")
        $lines.Add("- Config reload checks: $($Report.metrics.configReloads)/$($Report.requestedConfigReloadCount)")
        $lines.Add("- Maximum DSP call: $($Report.metrics.maximumDspUs) us")
        $lines.Add("- DSP deadline misses: $($Report.metrics.dspDeadlineMisses)/$($Report.metrics.dspCalls)")
        $lines.Add("- DSP critical stalls: $($Report.metrics.dspCriticalStalls)")
        $lines.Add("- Maximum render padding: $($Report.metrics.maximumRenderPadding)/$($Report.metrics.renderBufferFrames)")
    }
    if ($Report.error) {
        $lines.Add("- Error: $($Report.error)")
    }
    if ($Report.healthReadWarnings -and $Report.healthReadWarnings.Count -gt 0) {
        $lines.Add("- Health read warnings: $($Report.healthReadWarnings.Count)")
    }
    if ($Report.gates) {
        $lines.Add("")
        $lines.Add("## Gates")
        $lines.Add("")
        foreach ($gate in $Report.gates) {
            $mark = if ($gate.passed) { "PASS" } else { "FAIL" }
            $lines.Add("- **$mark** - $($gate.name): $($gate.detail)")
        }
    }
    if ($Report.healthAnalysis) {
        $lines.Add("")
        $lines.Add("## Health Analysis")
        $lines.Add("")
        $lines.Add("- Drop events: $(@($Report.healthAnalysis.dropEvents).Count)")
        $lines.Add("- Discontinuity events: $(@($Report.healthAnalysis.discontinuityEvents).Count)")
        $lines.Add("- Deadline events: $(@($Report.healthAnalysis.deadlineEvents).Count)")
    }
    if ($Report.environmentEvents) {
        $lines.Add("")
        $lines.Add("## Environment Event Windows")
        $lines.Add("")
        foreach ($window in $Report.environmentEvents) {
            $lines.Add("- $($window.reason): $($window.count) event(s), $($window.startUtc) to $($window.endUtc)")
        }
    }
    if ($Report.healthReadWarnings -and $Report.healthReadWarnings.Count -gt 0) {
        $lines.Add("")
        $lines.Add("## Health Read Warnings")
        $lines.Add("")
        foreach ($warning in $Report.healthReadWarnings) {
            $lines.Add("- $($warning.timestamp) [$($warning.stage)] $($warning.message)")
        }
    }
    $lines.Add("")
    $lines.Add("## Artifacts")
    $lines.Add("")
    $lines.Add("- Health history: " + $historyPath)
    $lines.Add("- JSON report: " + $reportJson)
    $lines.Add("- Isolated data root: " + $dataRoot)
    $lines | Set-Content $reportMarkdown -Encoding UTF8
}

try {
    if ($DurationMinutes -le 0) { throw "DurationMinutes must be greater than zero." }
    if ($CrashCount -lt 0 -or $ConfigReloadCount -lt 0) { throw "Event counts cannot be negative." }
    $powerStateAtStart = Get-PowerSourceState

    Stop-AxiomProcesses
    if (-not $SkipBuild -and [string]::IsNullOrWhiteSpace($ApplicationRoot)) {
        & (Join-Path $harness "publish_axiom_app.ps1")
        if ($LASTEXITCODE -ne 0) { throw "Package build failed." }
    }
    if (-not (Test-Path $controllerExe) -or -not (Test-Path $consoleExe)) {
        throw "Packaged controller or native processor is missing."
    }

    New-Item $runRoot -ItemType Directory -Force | Out-Null
    if (-not [string]::IsNullOrWhiteSpace($StateSeedPath)) {
        $resolvedStateSeed = [IO.Path]::GetFullPath($StateSeedPath)
        if (-not (Test-Path $resolvedStateSeed -PathType Leaf)) {
            throw "Controller state seed is missing: $resolvedStateSeed"
        }
        New-Item $dataRoot -ItemType Directory -Force | Out-Null
        Copy-Item $resolvedStateSeed (Join-Path $dataRoot "controller-state.json") -Force
    }
    $originalDefault = & $consoleExe --get-default-json | ConvertFrom-Json
    $devices = (& $consoleExe --list-devices-json | ConvertFrom-Json).devices

    $env:JAMESDSP_DATA_ROOT = $dataRoot
    $controllerProcess = Start-Process -FilePath $controllerExe -PassThru
    if (-not (Wait-ForPath (Join-Path $dataRoot "controller-state.json"))) {
        throw "Controller state was not created."
    }
    Start-Sleep -Seconds 2

    $state = Get-Content (Join-Path $dataRoot "controller-state.json") -Raw | ConvertFrom-Json
    $capture = $devices | Where-Object { $_.id -eq $state.CaptureId } | Select-Object -First 1
    $output = $devices | Where-Object { $_.id -eq $state.OutputId } | Select-Object -First 1
    if (-not $capture -or -not $output) { throw "The isolated controller route is incomplete." }

    & $consoleExe --set-default $capture.index | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Unable to set the soak capture endpoint as Windows default." }

    $button = Find-AxiomButton "Start Processor"
    if (-not $button) { throw "Start Processor was not found through UI Automation." }
    $button.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    $processor = Wait-ForProcessor
    if (-not $processor) { throw "The native processor did not start." }

    if (-not $SkipAudioProbe) {
        $ffplay = (Get-Command ffplay.exe -ErrorAction Stop).Source
        $probeProcess = Start-Process -FilePath $ffplay -ArgumentList @(
            "-nodisp", "-loglevel", "warning", "-f", "lavfi", "-i",
            "sine=frequency=220:sample_rate=48000", "-volume", "4"
        ) -RedirectStandardError $probeLogPath -WindowStyle Hidden -PassThru
        Start-Sleep -Seconds 3
        if ($probeProcess.HasExited) {
            $probeError = if (Test-Path $probeLogPath) { Get-Content $probeLogPath -Raw } else { "no FFplay log" }
            throw "The deterministic audio probe exited during startup: $probeError"
        }
    }

    Start-Sleep -Seconds $WarmupSeconds
    $duration = [TimeSpan]::FromMinutes($DurationMinutes)
    $deadline = (Get-Date).Add($duration)
    $eventSlots = [Math]::Max(1, $CrashCount + $ConfigReloadCount)
    $eventIntervalSeconds = [Math]::Max(8, $duration.TotalSeconds / ($eventSlots + 1))
    $nextEvent = (Get-Date).AddSeconds($eventIntervalSeconds)
    $crashesRemaining = $CrashCount
    $reloadsRemaining = $ConfigReloadCount
    $performCrashNext = $true
    $reloadValue = 4.5

    while ((Get-Date) -lt $deadline) {
        if (-not $SkipAudioProbe -and (!$probeProcess -or $probeProcess.HasExited)) {
            $probeError = if (Test-Path $probeLogPath) { Get-Content $probeLogPath -Raw } else { "no FFplay log" }
            throw "The deterministic audio probe stopped during the soak: $probeError"
        }
        $activeProcessors = @(Get-LiveProcessorProcesses)
        if ($activeProcessors.Count -ne 1) {
            $details = Get-ProcessorProcessDetails | ConvertTo-Json -Compress -Depth 3
            throw "Expected exactly one native processor, found $($activeProcessors.Count): $($activeProcessors.Id -join ', '). Process details: $details"
        }
        $activeProcessor = $activeProcessors[0]
        $observations.Add([ordered]@{
            timestamp = (Get-Date).ToUniversalTime().ToString("O")
            controllerRunning = $null -ne (Get-Process JamesDSPController -ErrorAction SilentlyContinue)
            processorPid = if ($activeProcessor) { $activeProcessor.Id } else { 0 }
            processorPids = @($activeProcessors.Id)
            healthSamples = @(Read-HealthSamples).Count
        })

        if ((Get-Date) -ge $nextEvent -and ($crashesRemaining -gt 0 -or $reloadsRemaining -gt 0)) {
            if (($performCrashNext -and $crashesRemaining -gt 0) -or $reloadsRemaining -eq 0) {
                if (-not $activeProcessor) { throw "No processor was available for the planned crash test." }
                $oldPid = $activeProcessor.Id
                Stop-Process -Id $oldPid -Force
                $replacement = Wait-ForProcessor -PreviousPid $oldPid
                $recovered = $null -ne $replacement
                $recoveryEvents.Add([ordered]@{
                    timestamp = (Get-Date).ToUniversalTime().ToString("O")
                    previousPid = $oldPid
                    replacementPid = if ($replacement) { $replacement.Id } else { 0 }
                    recovered = $recovered
                })
                if (-not $recovered) { throw "Processor crash recovery timed out." }
                $crashesRemaining -= 1
                $performCrashNext = $false
            } else {
                if (-not (Test-Path $configPath)) { throw "Runtime config is unavailable for reload testing." }
                $pidBefore = $activeProcessor.Id
                $configText = Get-Content $configPath -Raw
                if ($configText -notmatch "(?m)^postGain\s*=") {
                    throw "The runtime config does not expose the post-gain hot-reload setting."
                }
                $configText = [regex]::Replace(
                    $configText,
                    "(?m)^postGain\s*=.*$",
                    "postGain = $($reloadValue.ToString([Globalization.CultureInfo]::InvariantCulture))")
                Set-Content -Path $configPath -Value $configText -Encoding UTF8
                Start-Sleep -Seconds 3
                $afterReloadProcessors = @(Get-LiveProcessorProcesses)
                $survived = $null -ne (Get-Process -Id $pidBefore -ErrorAction SilentlyContinue)
                $reloadEvents.Add([ordered]@{
                    timestamp = (Get-Date).ToUniversalTime().ToString("O")
                    processorPid = $pidBefore
                    processorPidsAfter = @($afterReloadProcessors.Id)
                    parameter = "postGain"
                    value = $reloadValue
                    survived = $survived
                })
                if (-not $survived) { throw "Processor did not survive config hot reload." }
                if ($afterReloadProcessors.Count -ne 1) {
                    throw "Config reload left $($afterReloadProcessors.Count) native processors: $($afterReloadProcessors.Id -join ', ')."
                }
                $reloadValue = if ($reloadValue -eq 4.5) { 4.0 } else { 4.5 }
                $reloadsRemaining -= 1
                $performCrashNext = $true
            }
            $nextEvent = $nextEvent.AddSeconds($eventIntervalSeconds)
        }
        Start-Sleep -Seconds 2
    }

    Start-Sleep -Seconds 6
    $samples = @(Read-HealthSamples)
    $metrics = New-SoakMetrics $samples
    $gates = New-SoakGates $metrics
    $powerStateAtEnd = Get-PowerSourceState
    $powerSourceChanges = @(Get-PowerSourceChanges $startedAt)
    $audioPassed = @($gates | Where-Object { -not $_.passed }).Count -eq 0
    $environmentValid = $powerSourceChanges.Count -eq 0
    $result = if (-not $audioPassed) {
        "fail"
    } elseif (-not $environmentValid) {
        "pass_with_environment_warning"
    } else {
        "pass"
    }
    $endedAt = Get-Date
    $healthAnalysis = Get-HealthAnalysis $samples
    $environmentEvents = Get-EventWindowSummaries $healthAnalysis $endedAt
    $classification = New-SoakClassification $result $metrics @($gates) $endedAt
    $report = [ordered]@{
        schemaVersion = 1
        result = $result
        startedAt = $startedAt.ToUniversalTime().ToString("O")
        endedAt = $endedAt.ToUniversalTime().ToString("O")
        requestedDurationMinutes = $DurationMinutes
        requestedCrashCount = $CrashCount
        requestedConfigReloadCount = $ConfigReloadCount
        audioProbeEnabled = -not $SkipAudioProbe
        route = [ordered]@{
            capture = $capture
            output = $output
            originalDefault = $originalDefault
        }
        power = [ordered]@{
            start = $powerStateAtStart
            end = $powerStateAtEnd
            sourceChanges = $powerSourceChanges
            stable = $environmentValid
        }
        classification = $classification
        metrics = $metrics
        gates = $gates
        healthAnalysis = $healthAnalysis
        environmentEvents = $environmentEvents
        recoveryEvents = $recoveryEvents
        reloadEvents = $reloadEvents
        healthReadWarnings = $healthReadWarnings
        observations = $observations
        artifacts = [ordered]@{
            runRoot = $runRoot
            dataRoot = $dataRoot
            healthHistory = $historyPath
            jsonReport = $reportJson
            markdownReport = $reportMarkdown
        }
    }
    Write-Reports $report
    Write-Host "Soak result: $($report.result.ToUpperInvariant())"
    Write-Host "Report: $reportMarkdown"
    if (-not $audioPassed) { exit 1 }
}
catch {
    $endedAt = Get-Date
    $samples = @(Read-HealthSamples)
    $metrics = New-SoakMetrics $samples
    $gates = New-SoakGates $metrics
    $powerStateAtEnd = Get-PowerSourceState
    $powerSourceChanges = @(Get-PowerSourceChanges $startedAt)
    $healthAnalysis = Get-HealthAnalysis $samples
    $environmentEvents = Get-EventWindowSummaries $healthAnalysis $endedAt
    $classification = New-SoakClassification "error" $metrics @($gates) $endedAt $_.Exception.Message
    $failure = [ordered]@{
        schemaVersion = 1
        result = "error"
        startedAt = $startedAt.ToUniversalTime().ToString("O")
        endedAt = $endedAt.ToUniversalTime().ToString("O")
        requestedDurationMinutes = $DurationMinutes
        requestedCrashCount = $CrashCount
        requestedConfigReloadCount = $ConfigReloadCount
        error = $_.Exception.Message
        route = [ordered]@{
            capture = $capture
            output = $output
            originalDefault = $originalDefault
        }
        power = [ordered]@{
            start = $powerStateAtStart
            end = $powerStateAtEnd
            sourceChanges = $powerSourceChanges
            stable = $powerSourceChanges.Count -eq 0
        }
        classification = $classification
        metrics = $metrics
        gates = $gates
        healthAnalysis = $healthAnalysis
        environmentEvents = $environmentEvents
        recoveryEvents = $recoveryEvents
        reloadEvents = $reloadEvents
        healthReadWarnings = $healthReadWarnings
        observations = $observations
        artifacts = [ordered]@{
            runRoot = $runRoot
            dataRoot = $dataRoot
            healthHistory = $historyPath
            jsonReport = $reportJson
            markdownReport = $reportMarkdown
        }
    }
    Write-Reports $failure
    Write-Error $_
    exit 1
}
finally {
    if ($probeProcess -and -not $probeProcess.HasExited) {
        Stop-Process -Id $probeProcess.Id -Force -ErrorAction SilentlyContinue
    }
    if ($controllerProcess -and -not $controllerProcess.HasExited) {
        $null = $controllerProcess.CloseMainWindow()
        if (-not $controllerProcess.WaitForExit(5000)) {
            Stop-Process -Id $controllerProcess.Id -Force -ErrorAction SilentlyContinue
        }
    }
    Get-Process JamesDSPConsole,AxiomJamesDSPConsole -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    if ($originalDefault -and $null -ne $originalDefault.index -and $originalDefault.index -ge 0) {
        & $consoleExe --set-default $originalDefault.index | Out-Null
    }
    if ($null -eq $previousDataRoot) {
        Remove-Item Env:JAMESDSP_DATA_ROOT -ErrorAction SilentlyContinue
    } else {
        $env:JAMESDSP_DATA_ROOT = $previousDataRoot
    }
}
