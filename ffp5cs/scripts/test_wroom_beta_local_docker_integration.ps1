<#
.SYNOPSIS
  Test d'integration local: Docker serveur + ESP32 wroom-beta-local.

.DESCRIPTION
  - Demarre la stack Docker locale (optionnel) et smoke test (optionnel).
  - Genere un override local non versionne pour LOCAL_SERVER_BASE_URL.
  - Lance le test serie wroom-beta-local (upload + capture + assertions endpoint).
  - Verifie qu'au moins une insertion apparait dans ffp3Data2 ou ffp3Heartbeat2.

.EXAMPLE
  .\scripts\test_wroom_beta_local_docker_integration.ps1 -Port COM7
#>

param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$MonitorSeconds = 150,
    [int]$DbTimeoutSeconds = 180,
    [switch]$SkipDockerUp,
    [switch]$SkipSmoke,
    [switch]$SkipUpload,
    [switch]$SkipSerial,
    [string]$LocalServerBaseUrl
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir
$repoRoot = Split-Path -Parent (Split-Path -Parent $projectDir)
$serverRoot = Join-Path $repoRoot "serveur"
$dockerScript = Join-Path $serverRoot "tools/local-docker.ps1"
$serialTestScript = Join-Path $scriptDir "test_wroom_beta_local_serial.ps1"
$overrideHeader = Join-Path $projectDir "include/local_server_overrides.h"
$compose = Join-Path $serverRoot "docker-compose.local.yml"
$composeEnv = Join-Path $serverRoot ".env.docker.example"

function Get-LanIpv4 {
    $route = Get-NetRoute -DestinationPrefix "0.0.0.0/0" -ErrorAction SilentlyContinue |
        Sort-Object RouteMetric | Select-Object -First 1
    if ($route) {
        $ip = Get-NetIPAddress -AddressFamily IPv4 -InterfaceIndex $route.InterfaceIndex -ErrorAction SilentlyContinue |
            Where-Object {
                $_.IPAddress -notlike "127.*" -and
                ($_.IPAddress -like "192.168.*" -or $_.IPAddress -like "10.*" -or $_.IPAddress -like "172.1[6-9].*" -or $_.IPAddress -like "172.2?.*" -or $_.IPAddress -like "172.3[0-1].*")
            } |
            Select-Object -First 1
        if ($ip) { return $ip.IPAddress }
    }

    $fallback = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object {
            $_.IPAddress -notlike "127.*" -and
            ($_.IPAddress -like "192.168.*" -or $_.IPAddress -like "10.*" -or $_.IPAddress -like "172.1[6-9].*" -or $_.IPAddress -like "172.2?.*" -or $_.IPAddress -like "172.3[0-1].*")
        } |
        Select-Object -First 1
    if ($fallback) { return $fallback.IPAddress }

    throw "Impossible de determiner une IP LAN IPv4."
}

function Invoke-DbCount {
    param([Parameter(Mandatory = $true)][string]$TableName)

    $query = "SELECT COUNT(*) FROM iot_n3_local.$TableName;"
    $raw = docker compose --env-file $composeEnv -f $compose exec -T db `
        mysql -uroot -proot_local_iot_n3 --batch --skip-column-names -e $query
    if ($LASTEXITCODE -ne 0) {
        throw "Echec lecture DB pour table '$TableName'."
    }

    $value = ($raw | Select-Object -Last 1).ToString().Trim()
    if ($value -notmatch '^\d+$') {
        throw "Valeur count invalide pour '$TableName': '$value'"
    }
    return [int]$value
}

if (-not (Test-Path -LiteralPath $dockerScript)) {
    throw "Script introuvable: $dockerScript"
}
if (-not (Test-Path -LiteralPath $serialTestScript)) {
    throw "Script introuvable: $serialTestScript"
}

if ([string]::IsNullOrWhiteSpace($LocalServerBaseUrl)) {
    $lanIp = Get-LanIpv4
    $LocalServerBaseUrl = "http://${lanIp}:8082"
}

Write-Host "[beta-local] URL serveur local retenue: $LocalServerBaseUrl" -ForegroundColor Cyan

$overrideContent = @(
    "#pragma once",
    "",
    "// Fichier local non versionne, genere automatiquement.",
    ("#define LOCAL_SERVER_BASE_URL_OVERRIDE `"{0}`"" -f $LocalServerBaseUrl)
) -join "`r`n"
Set-Content -Path $overrideHeader -Value $overrideContent -Encoding UTF8
Write-Host "[beta-local] Override ecrit: $overrideHeader" -ForegroundColor Gray

Push-Location $serverRoot
try {
    if (-not $SkipDockerUp) {
        powershell -ExecutionPolicy Bypass -File $dockerScript -Action up
        if ($LASTEXITCODE -ne 0) {
            throw "Echec local-docker up."
        }
    } else {
        Write-Host "[beta-local] Docker up saute (--SkipDockerUp)." -ForegroundColor Yellow
    }

    if (-not $SkipSmoke) {
        powershell -ExecutionPolicy Bypass -File $dockerScript -Action smoke
        if ($LASTEXITCODE -ne 0) {
            throw "Echec local-docker smoke."
        }
    } else {
        Write-Host "[beta-local] Smoke saute (--SkipSmoke)." -ForegroundColor Yellow
    }
} finally {
    Pop-Location
}

$beforeData = Invoke-DbCount -TableName "ffp3Data2"
$beforeHeartbeat = Invoke-DbCount -TableName "ffp3Heartbeat2"
Write-Host ("[beta-local] DB avant test: ffp3Data2={0}, ffp3Heartbeat2={1}" -f $beforeData, $beforeHeartbeat) -ForegroundColor Cyan

if (-not $SkipSerial) {
    & $serialTestScript -Port $Port -MonitorSeconds $MonitorSeconds -SkipUpload:$SkipUpload
    if ($LASTEXITCODE -ne 0) {
        throw "Le test serie beta-local a echoue."
    }
} else {
    Write-Host "[beta-local] Test serie saute (--SkipSerial)." -ForegroundColor Yellow
}

$deadline = (Get-Date).AddSeconds($DbTimeoutSeconds)
$afterData = $beforeData
$afterHeartbeat = $beforeHeartbeat

while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 5
    $afterData = Invoke-DbCount -TableName "ffp3Data2"
    $afterHeartbeat = Invoke-DbCount -TableName "ffp3Heartbeat2"
    if ($afterData -gt $beforeData -or $afterHeartbeat -gt $beforeHeartbeat) {
        break
    }
}

Write-Host ("[beta-local] DB apres test: ffp3Data2={0}, ffp3Heartbeat2={1}" -f $afterData, $afterHeartbeat) -ForegroundColor Cyan

if ($afterData -le $beforeData -and $afterHeartbeat -le $beforeHeartbeat) {
    throw "Aucune insertion detectee dans ffp3Data2/ffp3Heartbeat2 dans la fenetre de validation."
}

Write-Host "[beta-local] Integration Docker + device OK." -ForegroundColor Green
