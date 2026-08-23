param(
    [double]$DurationMinutes = 480,
    [int]$ConfigReloadCount = 24,
    [string]$ApplicationRoot = "",
    [string]$StateSeedPath = "",
    [int]$PreflightObservationSeconds = 120
)

$ErrorActionPreference = "Stop"
$tests = Split-Path -Parent $MyInvocation.MyCommand.Path
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

function Get-AcSleepSeconds {
    $output = powercfg /query SCHEME_CURRENT SUB_SLEEP STANDBYIDLE
    $line = $output | Where-Object { $_ -match "Current AC Power Setting Index:\s+0x([0-9a-fA-F]+)" } |
        Select-Object -First 1
    if (-not $line) { throw "Unable to read the current AC sleep timeout." }
    return [Convert]::ToInt32(([regex]::Match($line, "0x([0-9a-fA-F]+)").Groups[1].Value), 16)
}

$originalAcSleepSeconds = Get-AcSleepSeconds
try {
    powercfg /setacvalueindex SCHEME_CURRENT SUB_SLEEP STANDBYIDLE 0 | Out-Null
    powercfg /setactive SCHEME_CURRENT | Out-Null

    & (Join-Path $tests "run_release_preflight.ps1") `
        -ApplicationRoot $ApplicationRoot `
        -PowerObservationSeconds $PreflightObservationSeconds `
        -QuietHostObservationSeconds $PreflightObservationSeconds `
        -RequireQuietHost
    if ($LASTEXITCODE -ne 0) {
        throw "Release preflight failed. Overnight qualification was not started."
    }

    & (Join-Path $tests "run_soak_test.ps1") `
        -ApplicationRoot $ApplicationRoot `
        -DurationMinutes $DurationMinutes `
        -CrashCount 0 `
        -ConfigReloadCount $ConfigReloadCount `
        -WarmupSeconds 12 `
        -StateSeedPath $StateSeedPath `
        -SkipBuild
    exit $LASTEXITCODE
}
finally {
    powercfg /setacvalueindex SCHEME_CURRENT SUB_SLEEP STANDBYIDLE $originalAcSleepSeconds | Out-Null
    powercfg /setactive SCHEME_CURRENT | Out-Null
}
