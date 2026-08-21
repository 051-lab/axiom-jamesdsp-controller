param(
    [string]$CertificateThumbprint = "",
    [string]$TimestampUrl = "http://timestamp.digicert.com",
    [switch]$VerifyOnly
)

$ErrorActionPreference = "Stop"
$harness = Split-Path -Parent $MyInvocation.MyCommand.Path
$package = Join-Path $harness "dist\JamesDSPController-win-x64"
$installerDir = Join-Path $harness "dist\installer"

$signtoolCandidates = @(
    Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Filter signtool.exe -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match "\\x64\\signtool\.exe$" } |
        Sort-Object FullName -Descending |
        Select-Object -ExpandProperty FullName
)
$signtool = $signtoolCandidates | Select-Object -First 1
if (-not $signtool) {
    throw "signtool.exe was not found. Install the Windows SDK signing tools."
}

$targets = @(
    (Join-Path $package "JamesDSPConsole.exe"),
    (Join-Path $package "JamesDSPController.exe"),
    (Join-Path $package "JamesDSPController.dll"),
    (Join-Path $package "JamesDSPController.Core.dll")
)
$installer = Get-ChildItem $installerDir -Filter "JamesDSPController-*-win-x64-setup.exe" |
    Sort-Object LastWriteTimeUtc -Descending |
    Select-Object -First 1
if ($installer) {
    $targets += $installer.FullName
}

$missing = @($targets | Where-Object { -not (Test-Path $_) })
if ($missing.Count -gt 0) {
    throw "Signing target files are missing: $($missing -join '; ')"
}

if (-not $VerifyOnly) {
    if ([string]::IsNullOrWhiteSpace($CertificateThumbprint)) {
        throw "CertificateThumbprint is required for signing."
    }
    $certificate = Get-ChildItem Cert:\CurrentUser\My,Cert:\LocalMachine\My -CodeSigningCert |
        Where-Object { $_.Thumbprint -eq $CertificateThumbprint.Replace(" ", "").ToUpperInvariant() } |
        Select-Object -First 1
    if (-not $certificate) {
        throw "No usable code-signing certificate matches thumbprint $CertificateThumbprint."
    }

    foreach ($target in $targets) {
        & $signtool sign /sha1 $certificate.Thumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 $target
        if ($LASTEXITCODE -ne 0) {
            throw "Signing failed for $target."
        }
    }
}

$results = foreach ($target in $targets) {
    $signature = Get-AuthenticodeSignature $target
    [ordered]@{
        path = $target
        status = [string]$signature.Status
        statusMessage = $signature.StatusMessage
        signer = if ($signature.SignerCertificate) { $signature.SignerCertificate.Subject } else { "" }
        thumbprint = if ($signature.SignerCertificate) { $signature.SignerCertificate.Thumbprint } else { "" }
        sha256 = (Get-FileHash $target -Algorithm SHA256).Hash
    }
}

$failed = @($results | Where-Object { $_.status -ne "Valid" })
$results | ConvertTo-Json -Depth 4
if ($failed.Count -gt 0) {
    throw "Authenticode verification failed for $($failed.Count) release file(s)."
}
