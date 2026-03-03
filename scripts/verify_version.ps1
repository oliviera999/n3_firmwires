<#
.SYNOPSIS
  Verify that the firmware version in include/config.h matches the latest entry in VERSION.md.

.PARAMETER VersionHeader
  Path to the header file containing the firmware version constant.

.PARAMETER VersionLog
  Path to the version history markdown file.

.EXAMPLE
  pwsh ./scripts/verify_version.ps1
#>

param(
    [string]$VersionHeader = "include/config.h",
    [string]$VersionLog = "VERSION.md"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $VersionHeader)) {
    Write-Error "Version header '$VersionHeader' introuvable."
}

if (-not (Test-Path $VersionLog)) {
    Write-Error "Journal de versions '$VersionLog' introuvable."
}

$headerContent = Get-Content $VersionHeader
$versionLine = $headerContent | Where-Object { $_ -match '\bconstexpr\s+const\s+char\*\s+VERSION\s*=\s*"([0-9]+\.[0-9]+)"' }

if (-not $versionLine) {
    Write-Error "Impossible de trouver la constante VERSION dans $VersionHeader."
}

$firmwareVersion = [regex]::Match($versionLine, '"([0-9]+\.[0-9]+)"').Groups[1].Value

$logContent = Get-Content $VersionLog
$logLine = $logContent | Where-Object { $_ -match '^##\s+Version\s+([0-9]+\.[0-9]+)' } | Select-Object -First 1

if (-not $logLine) {
    Write-Error "Impossible de trouver une entrée '## Version' dans $VersionLog."
}

$logVersion = [regex]::Match($logLine, '([0-9]+\.[0-9]+)').Groups[1].Value

if ($firmwareVersion -ne $logVersion) {
    Write-Error "La version firmware ($firmwareVersion) ne correspond pas à la dernière entrée du journal ($logVersion)."
} else {
    Write-Host "[VersionCheck] ✅ Version firmware $firmwareVersion synchronisée avec VERSION.md" -ForegroundColor Green
}

