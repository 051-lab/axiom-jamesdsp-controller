param(
    [string]$ApplicationRoot = "",
    [int]$PowerObservationSeconds = 30,
    [int]$QuietHostObservationSeconds = 60,
    [switch]$RequireQuietHost,
    [string]$JsonPath = ""
)

$ErrorActionPreference = "Stop"
$harness = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if ([string]::IsNullOrWhiteSpace($ApplicationRoot)) {
    $newRoot = Join-Path $env:ProgramFiles "JamesDSP Controller"
    $legacyRoot = Join-Path $env:ProgramFiles "Axiom JamesDSP Controller"
    $ApplicationRoot = if (Test-Path (Join-Path $newRoot "JamesDSPController.exe")) {
        $newRoot
    } elseif (Test-Path (Join-Path $legacyRoot "JamesDSPController.exe")) {
        $legacyRoot
    } else {
        $newRoot
    }
}
$package = Join-Path $harness "dist\JamesDSPController-win-x64"
$checks = [System.Collections.Generic.List[object]]::new()

function Add-Check([string]$Name, [bool]$Passed, [string]$Detail, [string]$Severity = "error") {
    $checks.Add([ordered]@{ name = $Name; passed = $Passed; detail = $Detail; severity = $Severity })
}

function Get-PowerState {
    $battery = Get-CimInstance -Namespace root\wmi -ClassName BatteryStatus -ErrorAction SilentlyContinue |
        Select-Object -First 1
    return [ordered]@{
        powerOnline = if ($battery) { [bool]$battery.PowerOnline } else { $null }
        charging = if ($battery) { [bool]$battery.Charging } else { $null }
        discharging = if ($battery) { [bool]$battery.Discharging } else { $null }
    }
}

function Get-PowerSettingSeconds([string]$Alias) {
    $output = powercfg /query SCHEME_CURRENT SUB_SLEEP $Alias
    $line = $output | Where-Object { $_ -match "Current AC Power Setting Index:\s+0x([0-9a-fA-F]+)" } |
        Select-Object -First 1
    if (-not $line) { return $null }
    return [Convert]::ToInt32(([regex]::Match($line, "0x([0-9a-fA-F]+)").Groups[1].Value), 16)
}

function Get-EventSummaries([datetime]$StartTime, [datetime]$EndTime) {
    $events = @(
        foreach ($log in @("System", "Application")) {
            Get-WinEvent -FilterHashtable @{
                LogName = $log
                StartTime = $StartTime
                EndTime = $EndTime
            } -ErrorAction SilentlyContinue |
                Where-Object {
                    $_.ProviderName -match "Hyper-V-VmSwitch|BTHUSB|Windows Error Reporting|Kernel-Power|HP Comm Recovery|WindowsUpdate|Display|WHEA|Power" -or
                    $_.LevelDisplayName -in @("Warning", "Error", "Critical")
                } |
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
    return @($events)
}

$startedAt = Get-Date
$powerStart = Get-PowerState
$observationSeconds = [Math]::Max($PowerObservationSeconds, $QuietHostObservationSeconds)
if ($observationSeconds -gt 0) {
    Start-Sleep -Seconds $observationSeconds
}
$endedAt = Get-Date
$powerEnd = Get-PowerState
$powerChanges = @(
    Get-WinEvent -FilterHashtable @{
        LogName = "System"
        ProviderName = "Microsoft-Windows-Kernel-Power"
        Id = 105
        StartTime = $startedAt
    } -ErrorAction SilentlyContinue
)

Add-Check "AC power is online" ($powerStart.powerOnline -eq $true -and $powerEnd.powerOnline -eq $true) (
    "start=$($powerStart.powerOnline); end=$($powerEnd.powerOnline)")
Add-Check "power source remained stable during observation" ($powerChanges.Count -eq 0) (
    "changes=$($powerChanges.Count); observationSeconds=$observationSeconds")

$quietEvents = Get-EventSummaries $startedAt $endedAt
$quietEventDetail = if ($quietEvents.Count -eq 0) {
    "none; observationSeconds=$observationSeconds"
} else {
    (($quietEvents |
        Group-Object provider |
        Sort-Object Count -Descending |
        Select-Object -First 6 |
        ForEach-Object { "$($_.Name)=$($_.Count)" }) -join "; ") + "; observationSeconds=$observationSeconds"
}
$quietSeverity = if ($RequireQuietHost) { "error" } else { "warn" }
Add-Check "quiet host observation has no relevant system churn" ($quietEvents.Count -eq 0) $quietEventDetail $quietSeverity

$activeScheme = powercfg /getactivescheme
Add-Check "High performance power scheme is active" ($activeScheme -match "High performance") (
    ($activeScheme -join " ").Trim())

$sleepSeconds = Get-PowerSettingSeconds "STANDBYIDLE"
$hibernateSeconds = Get-PowerSettingSeconds "HIBERNATEIDLE"
Add-Check "AC sleep is disabled" ($sleepSeconds -eq 0) "seconds=$sleepSeconds"
Add-Check "AC hibernate is disabled" ($hibernateSeconds -eq 0) "seconds=$hibernateSeconds"

$pendingRebootKeys = @(
    "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsUpdate\Auto Update\RebootRequired",
    "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Component Based Servicing\RebootPending"
)
$pendingReboots = @($pendingRebootKeys | Where-Object { Test-Path $_ })
$pendingRebootDetail = if ($pendingReboots.Count -eq 0) {
    "none"
} else {
    $pendingReboots -join "; "
}
Add-Check "no pending Windows reboot" ($pendingReboots.Count -eq 0) $pendingRebootDetail

$installedConsole = Join-Path $ApplicationRoot "JamesDSPConsole.exe"
$installedController = Join-Path $ApplicationRoot "JamesDSPController.dll"
$installedControllerCore = Join-Path $ApplicationRoot "JamesDSPController.Core.dll"
$packageConsole = Join-Path $package "JamesDSPConsole.exe"
$packageController = Join-Path $package "JamesDSPController.dll"
$packageControllerCore = Join-Path $package "JamesDSPController.Core.dll"
$releaseFiles = @(
    $installedConsole,
    $installedController,
    $installedControllerCore,
    $packageConsole,
    $packageController,
    $packageControllerCore
)
foreach ($path in $releaseFiles) {
    Add-Check "file exists: $([IO.Path]::GetFileName($path)) [$path]" (Test-Path $path) $path
}
if (@($releaseFiles | Where-Object { -not (Test-Path $_) }).Count -eq 0) {
    $consoleMatches = (Get-FileHash $installedConsole -Algorithm SHA256).Hash -eq
        (Get-FileHash $packageConsole -Algorithm SHA256).Hash
    $controllerMatches = (Get-FileHash $installedController -Algorithm SHA256).Hash -eq
        (Get-FileHash $packageController -Algorithm SHA256).Hash
    $controllerCoreMatches = (Get-FileHash $installedControllerCore -Algorithm SHA256).Hash -eq
        (Get-FileHash $packageControllerCore -Algorithm SHA256).Hash
    Add-Check "installed native processor matches package" $consoleMatches "SHA-256 comparison"
    Add-Check "installed controller matches package" $controllerMatches "SHA-256 comparison"
    Add-Check "installed controller core matches package" $controllerCoreMatches "SHA-256 comparison"
}

$activeAxiom = @(Get-Process JamesDSPController,JamesDSPConsole,AxiomJamesDSPController,AxiomJamesDSPConsole -ErrorAction SilentlyContinue)
$activeAxiomDetail = if ($activeAxiom.Count -eq 0) {
    "none"
} else {
    $activeAxiom.Name -join ", "
}
Add-Check "no competing JamesDSP Controller processes are running" ($activeAxiom.Count -eq 0) $activeAxiomDetail

$consoleForRoute = if (Test-Path $installedConsole) { $installedConsole } else { $packageConsole }
if (Test-Path $consoleForRoute) {
    $defaultRoute = & $consoleForRoute --get-default-json | ConvertFrom-Json
    Add-Check "Windows default route is available" ($defaultRoute.index -ge 0) (
        "[$($defaultRoute.index)] $($defaultRoute.name)")
}

$passed = @($checks | Where-Object { -not $_.passed -and $_.severity -ne "warn" }).Count -eq 0
$report = [ordered]@{
    schemaVersion = 1
    result = if ($passed) { "pass" } else { "fail" }
    timestampUtc = (Get-Date).ToUniversalTime().ToString("O")
    applicationRoot = $ApplicationRoot
    packageRoot = $package
    powerObservationSeconds = $PowerObservationSeconds
    quietHostObservationSeconds = $QuietHostObservationSeconds
    requireQuietHost = [bool]$RequireQuietHost
    quietHostEvents = $quietEvents
    checks = $checks
}

if ([string]::IsNullOrWhiteSpace($JsonPath)) {
    $JsonPath = Join-Path $env:LOCALAPPDATA "JamesDSP\SoakTests\release-preflight.json"
}
New-Item (Split-Path -Parent $JsonPath) -ItemType Directory -Force | Out-Null
$report | ConvertTo-Json -Depth 6 | Set-Content $JsonPath -Encoding UTF8

foreach ($check in $checks) {
    $label = if ($check.passed) { "PASS" } elseif ($check.severity -eq "warn") { "WARN" } else { "FAIL" }
    Write-Host ("{0}: {1} - {2}" -f $label, $check.name, $check.detail)
}
Write-Host "Preflight report: $JsonPath"
if (-not $passed) { exit 1 }
