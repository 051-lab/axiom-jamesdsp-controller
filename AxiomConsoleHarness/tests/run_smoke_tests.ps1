param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$harness = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$package = Join-Path $harness "dist\JamesDSPController-win-x64"
$controller = Join-Path $package "JamesDSPController.exe"
$console = Join-Path $package "JamesDSPConsole.exe"
$installerDir = Join-Path $harness "dist\installer"
$testData = Join-Path $env:LOCALAPPDATA "JamesDSP\AutomatedSmoke"

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "ASSERTION FAILED: $Message" }
    Write-Host "PASS: $Message"
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

try {
    Stop-AxiomProcesses
    if (-not $SkipBuild) {
        & (Join-Path $harness "publish_axiom_app.ps1")
        if ($LASTEXITCODE -ne 0) { throw "Package build failed." }
        & (Join-Path $harness "build_installer.ps1") -SkipPackage
        if ($LASTEXITCODE -ne 0) { throw "Installer build failed." }
    }

    Assert-True (Test-Path $controller) "packaged controller exists"
    Assert-True (Test-Path $console) "packaged native processor exists"
    Assert-True (Test-Path (Join-Path $package "JamesDSPController.Core.dll")) "packaged controller core exists"
    Assert-True (Test-Path (Join-Path $package "assets\Liveprog\axiom_binaural_dsp_v4.1.4.11.eel")) "accepted R011 EEL is bundled"
    Assert-True (Test-Path (Join-Path $package "runtime\axiom-test-lowcut.eel")) "low-cut test is bundled"
    Assert-True (Test-Path (Join-Path $package "runtime\axiom-test-pulse-gate.eel")) "pulse test is bundled"
    Assert-True ((Get-Content (Join-Path $package "runtime\axiom-test-lowcut.eel") -Raw).Contains("// UI Defaults")) "low-cut test contains UI defaults"
    Assert-True ((Get-Content (Join-Path $package "runtime\axiom-test-pulse-gate.eel") -Raw).Contains("// UI Defaults")) "pulse test contains UI defaults"
    Assert-True ((Get-ChildItem $installerDir -Filter "JamesDSPController-*-setup.exe").Count -gt 0) "installer executable exists"

    $devices = (& $console --list-devices-json | ConvertFrom-Json).devices
    Assert-True (@($devices | Where-Object { $_.name -eq "CABLE Input (VB-Audio Virtual Cable)" }).Count -eq 1) "stereo VB-CABLE endpoint is visible"
    Assert-True (@($devices | Where-Object { $_.name -match "Realtek|EarPods" }).Count -gt 0) "physical output endpoint is visible"
    $default = & $console --get-default-json | ConvertFrom-Json
    Assert-True (-not [string]::IsNullOrWhiteSpace($default.id)) "current Windows default endpoint is reported by stable ID"

    if (Test-Path $testData) { Remove-Item $testData -Recurse -Force }
    $env:JAMESDSP_DATA_ROOT = $testData
    Start-Process -FilePath $controller
    Start-Sleep -Seconds 3
    Assert-True (Test-Path (Join-Path $testData "controller-state.json")) "clean first run creates controller state"
    $defaultConfig = Get-Content (Join-Path $testData "jamesdsp-controller.ini") -Raw
    Assert-True ($defaultConfig -match "(?ms)^\[LiveProg\].*?^enabled\s*=\s*false\s*$") "clean first run leaves LiveProg disabled"

    $state = Get-Content (Join-Path $testData "controller-state.json") -Raw | ConvertFrom-Json
    $capture = $devices | Where-Object { $_.id -eq $state.CaptureId }
    $output = $devices | Where-Object { $_.id -eq $state.OutputId }
    Assert-True ($capture.name -eq "CABLE Input (VB-Audio Virtual Cable)") "clean first run selects stereo VB-CABLE source"
    Assert-True ($output.name -match "Realtek|EarPods") "clean first run selects a physical output"

    Start-Process -FilePath $controller
    Start-Sleep -Milliseconds 800
    Assert-True ((Get-Process JamesDSPController).Count -eq 1) "controller enforces single-instance behavior"

    $button = Find-AxiomButton "Start Processor"
    Assert-True ($null -ne $button) "Routing tab is first and exposes Start Processor"
    $invoke = $button.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
    $invoke.Invoke()
    Start-Sleep -Seconds 3
    $firstProcessor = Get-Process JamesDSPConsole -ErrorAction SilentlyContinue
    Assert-True ($null -ne $firstProcessor) "controller starts the native processor"

    $firstPid = $firstProcessor.Id
    Stop-Process -Id $firstPid -Force
    Start-Sleep -Seconds 4
    $recovered = Get-Process JamesDSPConsole -ErrorAction SilentlyContinue
    Assert-True ($null -ne $recovered -and $recovered.Id -ne $firstPid) "processor automatically recovers from one forced crash"

    Start-Sleep -Seconds 6
    $historyPath = Join-Path $testData "diagnostics\health-history.jsonl"
    Assert-True (Test-Path $historyPath) "processor telemetry creates persistent health history"
    Assert-True (@(Get-Content $historyPath | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }).Count -gt 0) "persistent health history contains samples"

    Get-ChildItem (Join-Path $harness "profiles") -Filter "*.json" -ErrorAction SilentlyContinue |
        ForEach-Object {
            $null = Get-Content $_.FullName -Raw | ConvertFrom-Json
            Write-Host "PASS: valid profile JSON - $($_.Name)"
        }

    Write-Host "All JamesDSP Controller Windows smoke tests passed."
}
finally {
    Stop-AxiomProcesses
    Remove-Item Env:JAMESDSP_DATA_ROOT -ErrorAction SilentlyContinue
}
