<#
.SYNOPSIS
  Lance la batterie de tests wroom-beta-local (quick/full, token/session/both).

.EXAMPLE
  .\scripts\run_wroom_beta_local_test_suite.ps1 -Port COM5 -Campaign quick -Auth both
#>

param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [ValidateSet("quick", "full", "both")]
    [string]$Campaign = "quick",
    [ValidateSet("token", "session", "both")]
    [string]$Auth = "both",
    [switch]$SkipUpload,
    [switch]$SkipDockerUp,
    [string]$LocalSecretsFile = "",
    [string]$ScenarioFile = ""
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir
$integrationScript = Join-Path $scriptDir "test_wroom_beta_local_docker_integration.ps1"
$unitScript = Join-Path $scriptDir "test_unit_all.ps1"
$defaultSecrets = Join-Path $scriptDir ".beta-local-test.env"
$defaultScenarioFile = Join-Path $scriptDir "wroom_beta_local_test_scenarios.json"
$logsDir = Join-Path $projectDir "logs"

if ([string]::IsNullOrWhiteSpace($LocalSecretsFile)) {
    $LocalSecretsFile = $defaultSecrets
}
if ([string]::IsNullOrWhiteSpace($ScenarioFile)) {
    $ScenarioFile = $defaultScenarioFile
}

if (-not (Test-Path -LiteralPath $integrationScript)) { throw "Script introuvable: $integrationScript" }
if (-not (Test-Path -LiteralPath $unitScript)) { throw "Script introuvable: $unitScript" }
if (-not (Test-Path -LiteralPath $ScenarioFile)) { throw "Scenario file introuvable: $ScenarioFile" }
if (-not (Test-Path -LiteralPath $logsDir)) { New-Item -ItemType Directory -Path $logsDir | Out-Null }

function Import-LocalSecrets {
    param([string]$FilePath)
    $map = @{}
    if (-not (Test-Path -LiteralPath $FilePath)) { return $map }
    foreach ($line in (Get-Content -LiteralPath $FilePath -ErrorAction Stop)) {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith("#")) { continue }
        $idx = $trimmed.IndexOf("=")
        if ($idx -lt 1) { continue }
        $k = $trimmed.Substring(0, $idx).Trim()
        $v = $trimmed.Substring($idx + 1).Trim().Trim('"')
        $map[$k] = $v
    }
    return $map
}

function Get-RequestedCampaigns {
    switch ($Campaign) {
        "both" { return @("quick", "full") }
        default { return @($Campaign) }
    }
}

function Get-RequestedAuths {
    switch ($Auth) {
        "both" { return @("token", "session") }
        default { return @($Auth) }
    }
}

function Assert-SecretPrechecks {
    param(
        [hashtable]$Secrets,
        [string[]]$AuthModes
    )

    $hasToken = $Secrets.ContainsKey("N3_TEST_ADMIN_TOKEN") -and -not [string]::IsNullOrWhiteSpace($Secrets["N3_TEST_ADMIN_TOKEN"])
    $hasUsername = $Secrets.ContainsKey("N3_TEST_ADMIN_USERNAME") -and -not [string]::IsNullOrWhiteSpace($Secrets["N3_TEST_ADMIN_USERNAME"])
    $hasPassword = $Secrets.ContainsKey("N3_TEST_ADMIN_PASSWORD") -and -not [string]::IsNullOrWhiteSpace($Secrets["N3_TEST_ADMIN_PASSWORD"])
    $hasHash = $Secrets.ContainsKey("N3_TEST_ADMIN_PASSWORD_HASH") -and -not [string]::IsNullOrWhiteSpace($Secrets["N3_TEST_ADMIN_PASSWORD_HASH"])
    $hasApiKey = $Secrets.ContainsKey("N3_TEST_API_KEY") -and -not [string]::IsNullOrWhiteSpace($Secrets["N3_TEST_API_KEY"])

    if (($AuthModes -contains "token") -and -not $hasToken) {
        throw "Pre-check: N3_TEST_ADMIN_TOKEN manquant dans $LocalSecretsFile."
    }
    if (($AuthModes -contains "session") -and -not $hasUsername) {
        throw "Pre-check: N3_TEST_ADMIN_USERNAME manquant dans $LocalSecretsFile."
    }
    if (($AuthModes -contains "session") -and (-not $hasPassword -and -not $hasHash)) {
        throw "Pre-check: N3_TEST_ADMIN_PASSWORD ou N3_TEST_ADMIN_PASSWORD_HASH requis pour session."
    }
    if (-not $hasApiKey) {
        throw "Pre-check: N3_TEST_API_KEY manquant dans $LocalSecretsFile."
    }
}

function Invoke-Step {
    param(
        [string]$Name,
        [string]$ScriptPath,
        [string[]]$ScriptArguments
    )

    $start = Get-Date
    Write-Host "[suite] START $Name" -ForegroundColor Cyan
    # Un tableau splatte avec & $Script ne lie pas -Nom valeur ; un second powershell avec -File + argv le fait.
    $allArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $ScriptPath) + $ScriptArguments
    $p = Start-Process -FilePath powershell.exe -ArgumentList $allArgs -Wait -PassThru -NoNewWindow
    $code = $p.ExitCode
    if ($null -eq $code) { $code = 0 }
    $output = @()
    $durationSec = [math]::Round(((Get-Date) - $start).TotalSeconds, 1)
    $status = if ($code -eq 0) { "passed" } else { "failed" }
    Write-Host "[suite] END   $Name => $status (${durationSec}s)" -ForegroundColor $(if ($code -eq 0) { "Green" } else { "Red" })
    return [pscustomobject]@{
        name = $Name
        status = $status
        exitCode = $code
        durationSec = $durationSec
        output = ($output -join "`n")
    }
}

$secrets = Import-LocalSecrets -FilePath $LocalSecretsFile
$requestedCampaigns = Get-RequestedCampaigns
$requestedAuths = Get-RequestedAuths
Assert-SecretPrechecks -Secrets $secrets -AuthModes $requestedAuths

$allScenarios = Get-Content -LiteralPath $ScenarioFile -Raw | ConvertFrom-Json
$selectedScenarios = @(
    $allScenarios | Where-Object {
        ($requestedCampaigns -contains $_.campaign) -and
        ($requestedAuths -contains $_.auth)
    }
)

if ($selectedScenarios.Count -eq 0) {
    throw "Aucun scenario selectionne (Campaign=$Campaign, Auth=$Auth)."
}

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$results = @()

# Step commun: tests unitaires natifs une seule fois.
$results += Invoke-Step -Name "unit-native" -ScriptPath $unitScript -ScriptArguments @()
if ($results[-1].exitCode -ne 0) {
    throw "Arret: tests unitaires natifs en echec."
}

$idx = 0
foreach ($scenario in $selectedScenarios) {
    $idx++
    $stepName = "{0}/{1}-{2}" -f $idx, $selectedScenarios.Count, $scenario.id
    $scenarioArgs = @(
        "-Port", $Port,
        "-MonitorSeconds", [string]$scenario.monitorSeconds,
        "-AuthMode", [string]$scenario.auth,
        "-LocalSecretsFile", $LocalSecretsFile
    )
    if ($SkipUpload -or $idx -gt 1) {
        $scenarioArgs += "-SkipUpload"
    }
    if ($SkipDockerUp -and $idx -eq 1) {
        $scenarioArgs += "-SkipDockerUp"
    }
    if ([bool]$scenario.requireHeartbeat) {
        $scenarioArgs += "-RequireHeartbeat"
    }
    if ([bool]$scenario.runNegativeAuthChecks) {
        $scenarioArgs += "-RunNegativeAuthChecks"
    }

    $results += Invoke-Step -Name $stepName -ScriptPath $integrationScript -ScriptArguments $scenarioArgs
}

$report = [pscustomobject]@{
    timestamp = (Get-Date).ToString("s")
    port = $Port
    campaign = $Campaign
    auth = $Auth
    scenarioFile = $ScenarioFile
    localSecretsFile = $LocalSecretsFile
    results = $results
}

$reportPath = Join-Path $logsDir ("beta_local_suite_report_{0}.json" -f $timestamp)
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8

$failed = @($results | Where-Object { $_.exitCode -ne 0 }).Count
Write-Host ""
Write-Host "[suite] Rapport: $reportPath" -ForegroundColor Gray
if ($failed -gt 0) {
    Write-Host "[suite] Echec: $failed etape(s) en erreur." -ForegroundColor Red
    exit 1
}

Write-Host "[suite] OK: toutes les etapes sont passees." -ForegroundColor Green
exit 0
