param(
    [switch]$SkipPackage
)

$ErrorActionPreference = "Stop"
$harness = Split-Path -Parent $MyInvocation.MyCommand.Path
$definition = Join-Path $harness "installer\AxiomJamesDSPController.iss"
$versionProps = Join-Path $harness "Directory.Build.props"
$compilerCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
    (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe"),
    (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe")
)
$compiler = $compilerCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $compiler) {
    throw "Inno Setup 6 compiler was not found. Install JRSoftware.InnoSetup through winget."
}
if (-not (Test-Path $versionProps)) {
    throw "Central JamesDSP Controller version metadata is missing: $versionProps"
}

[xml]$versionDocument = Get-Content $versionProps -Raw
$version = [string]$versionDocument.Project.PropertyGroup.JamesDSPControllerVersion
if ($version -notmatch "^\d+\.\d+\.\d+$") {
    throw "JamesDSPControllerVersion must use semantic major.minor.patch format."
}

if (-not $SkipPackage) {
    & (Join-Path $harness "publish_axiom_app.ps1")
    if ($LASTEXITCODE -ne 0) {
        throw "Portable package build failed with exit code $LASTEXITCODE."
    }
}

& $compiler "/DMyAppVersion=$version" $definition
if ($LASTEXITCODE -ne 0) {
    throw "Installer compilation failed with exit code $LASTEXITCODE."
}

Write-Host "Installer version: $version"
Write-Host "Installer created under: $(Join-Path $harness 'dist\installer')"
