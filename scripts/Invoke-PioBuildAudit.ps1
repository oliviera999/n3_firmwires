param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectRoot,

    [Parameter(Mandatory = $true)]
    [string]$Environment,

    [string[]]$PioArguments = @(),
    [switch]$ScanOnly,
    [switch]$ExpectFirmwareBin,
    [string]$OutFile = "",
    [int]$MaxDurationSeconds = 900,
    [long]$MaxFirmwareBinBytes = 0,
    [double]$MaxBuildGrowthPercent = 250.0,
    [int]$MaxPathLength = 240,
    [int]$MinFreeSpaceMB = 500,
    [string]$BaselinePath = "",
    [switch]$UpdateBaseline,
    [double]$MaxBaselineDeltaPercent = 20.0,
    [switch]$ParsePioStats,
    [switch]$FailOnAnomaly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-PathLength {
    param([Parameter(Mandatory = $true)][string]$Path)
    return $Path.Length
}

function Get-SafeRelativePath {
    param(
        [Parameter(Mandatory = $true)][string]$FullPath,
        [Parameter(Mandatory = $true)][string]$RootPath
    )
    try {
        $baseUri = [System.Uri]((Resolve-Path -LiteralPath $RootPath).Path.TrimEnd('\') + '\')
        $fileUri = [System.Uri](Resolve-Path -LiteralPath $FullPath).Path
        return [System.Uri]::UnescapeDataString($baseUri.MakeRelativeUri($fileUri).ToString())
    } catch {
        return $FullPath
    }
}

function Get-DirectoryMetrics {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [int]$PathLimit = 240
    )

    $result = [ordered]@{
        Path = $Path
        Exists = (Test-Path -LiteralPath $Path)
        FileCount = 0L
        TotalBytes = 0L
        LongestPathLength = 0
        LongestPath = ""
        PathsOverLimit = @()
    }

    if (-not $result.Exists) {
        return [pscustomobject]$result
    }

    $items = Get-ChildItem -LiteralPath $Path -Recurse -File -Force -ErrorAction SilentlyContinue
    foreach ($item in $items) {
        $len = Get-PathLength -Path $item.FullName
        $result.FileCount++
        $result.TotalBytes += $item.Length
        if ($len -gt $result.LongestPathLength) {
            $result.LongestPathLength = $len
            $result.LongestPath = $item.FullName
        }
        if ($len -gt $PathLimit) {
            $result.PathsOverLimit += $item.FullName
        }
    }

    return [pscustomobject]$result
}

function Get-ArtifactInfo {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return [pscustomobject]@{
            Path = $Path
            Exists = $false
            SizeBytes = 0L
            LastWriteUtc = $null
        }
    }

    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    return [pscustomobject]@{
        Path = $Path
        Exists = $true
        SizeBytes = [long]$item.Length
        LastWriteUtc = $item.LastWriteTimeUtc
    }
}

function Get-DiskFreeSpace {
    param([Parameter(Mandatory = $true)][string]$Path)
    try {
        $root = [System.IO.Path]::GetPathRoot($Path)
        $drive = [System.IO.DriveInfo]::new($root)
        return [pscustomobject]@{
            Root = $root
            FreeBytes = [long]$drive.AvailableFreeSpace
            TotalBytes = [long]$drive.TotalSize
        }
    } catch {
        return [pscustomobject]@{
            Root = ""
            FreeBytes = -1L
            TotalBytes = -1L
        }
    }
}

function Get-DeltaMetrics {
    param(
        [Parameter(Mandatory = $true)]$Before,
        [Parameter(Mandatory = $true)]$After
    )
    $deltaBytes = [long]($After.TotalBytes - $Before.TotalBytes)
    $deltaFiles = [long]($After.FileCount - $Before.FileCount)
    $deltaPercent = 0.0
    if ($Before.TotalBytes -gt 0) {
        $deltaPercent = (($deltaBytes / [double]$Before.TotalBytes) * 100.0)
    } elseif ($After.TotalBytes -gt 0) {
        $deltaPercent = 100.0
    }

    return [pscustomobject]@{
        DeltaBytes = $deltaBytes
        DeltaFiles = $deltaFiles
        DeltaPercent = $deltaPercent
    }
}

function Format-Bytes {
    param([long]$Value)
    if ($Value -lt 1KB) { return "$Value B" }
    if ($Value -lt 1MB) { return ("{0:N2} KB" -f ($Value / 1KB)) }
    if ($Value -lt 1GB) { return ("{0:N2} MB" -f ($Value / 1MB)) }
    return ("{0:N2} GB" -f ($Value / 1GB))
}

function Test-ShouldExpectFirmware {
    param(
        [string[]]$BuildArgs,
        [bool]$Forced
    )
    if ($Forced) { return $true }
    if (-not $BuildArgs -or $BuildArgs.Count -eq 0) { return $true }

    $targets = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $BuildArgs.Count; $i++) {
        $current = $BuildArgs[$i]
        if ($current -eq "-t" -and ($i + 1) -lt $BuildArgs.Count) {
            $targets.Add($BuildArgs[$i + 1].ToLowerInvariant())
            $i++
            continue
        }
        if ($current -like "--target=*") {
            $targets.Add(($current.Split("=", 2)[1]).ToLowerInvariant())
        }
    }

    if ($targets.Count -eq 0) {
        return $true
    }
    return -not ($targets | Where-Object { $_ -ne "clean" } | Measure-Object).Count -eq 0
}

function Get-BaselineData {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    try {
        return (Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json)
    } catch {
        Write-Warning "Baseline JSON illisible: $Path"
        return $null
    }
}

function Save-BaselineData {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$ProjectRootPath,
        [Parameter(Mandatory = $true)][string]$EnvName,
        [Parameter(Mandatory = $true)][long]$FirmwareSize
    )
    $obj = [ordered]@{
        projectRoot = $ProjectRootPath
        environment = $EnvName
        firmwareBinBytes = $FirmwareSize
        updatedUtc = [DateTime]::UtcNow.ToString("o")
    }
    $dir = Split-Path -Path $Path -Parent
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -Path $dir -ItemType Directory -Force | Out-Null
    }
    $obj | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $Path -Encoding UTF8
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$helpersPath = Join-Path $scriptRoot "Get-PioBuildHelpers.ps1"
if (-not (Test-Path -LiteralPath $helpersPath)) {
    throw "Helpers introuvables: $helpersPath"
}
. $helpersPath

$resolvedProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$buildDir = Get-N3PioEnvBuildDir -ProjectRoot $resolvedProjectRoot -Environment $Environment
$libdepsDir = Join-Path (Join-Path $resolvedProjectRoot ".pio\libdeps") $Environment

if (-not $ScanOnly -and ($PioArguments.Count -eq 0)) {
    $PioArguments = @("run", "-e", $Environment)
}

$beforeBuildDir = Get-DirectoryMetrics -Path $buildDir -PathLimit $MaxPathLength
$beforeLibdeps = Get-DirectoryMetrics -Path $libdepsDir -PathLimit $MaxPathLength
$diskBefore = Get-DiskFreeSpace -Path $buildDir

$pioExitCode = 0
$durationSeconds = 0.0
$pioOutputLines = @()
$tempPioLog = ""

if (-not $ScanOnly) {
    $tempPioLog = Join-Path ([System.IO.Path]::GetTempPath()) ("pio-build-audit-" + [Guid]::NewGuid().ToString("N") + ".log")
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    Push-Location $resolvedProjectRoot
    try {
        $oldEap = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            & pio @PioArguments 2>&1 | Tee-Object -FilePath $tempPioLog
            $pioExitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $oldEap
        }
    } finally {
        Pop-Location
        $sw.Stop()
        $durationSeconds = [math]::Round($sw.Elapsed.TotalSeconds, 2)
    }
    if (Test-Path -LiteralPath $tempPioLog) {
        $pioOutputLines = Get-Content -LiteralPath $tempPioLog -ErrorAction SilentlyContinue
    }
}

$afterBuildDir = Get-DirectoryMetrics -Path $buildDir -PathLimit $MaxPathLength
$afterLibdeps = Get-DirectoryMetrics -Path $libdepsDir -PathLimit $MaxPathLength
$diskAfter = Get-DiskFreeSpace -Path $buildDir
$buildDelta = Get-DeltaMetrics -Before $beforeBuildDir -After $afterBuildDir
$libdepsDelta = Get-DeltaMetrics -Before $beforeLibdeps -After $afterLibdeps

$firmwareInfo = Get-ArtifactInfo -Path (Join-Path $buildDir "firmware.bin")
$elfInfo = Get-ArtifactInfo -Path (Join-Path $buildDir "firmware.elf")
$littlefsInfo = Get-ArtifactInfo -Path (Join-Path $buildDir "littlefs.bin")

$alerts = New-Object System.Collections.Generic.List[string]
$warnings = New-Object System.Collections.Generic.List[string]

if (-not $ScanOnly -and $pioExitCode -ne 0) {
    $alerts.Add("Code retour pio non nul: $pioExitCode.")
}

$mustExpectFirmware = Test-ShouldExpectFirmware -BuildArgs $PioArguments -Forced $ExpectFirmwareBin.IsPresent
if ($mustExpectFirmware -and -not $firmwareInfo.Exists) {
    $alerts.Add("firmware.bin absent apres la commande.")
}

if ($MaxFirmwareBinBytes -gt 0 -and $firmwareInfo.Exists -and $firmwareInfo.SizeBytes -gt $MaxFirmwareBinBytes) {
    $alerts.Add("firmware.bin depasse le seuil: $($firmwareInfo.SizeBytes) bytes > $MaxFirmwareBinBytes bytes.")
}

if (-not $ScanOnly -and $durationSeconds -gt $MaxDurationSeconds) {
    $alerts.Add("Duree de build anormale: $durationSeconds s > $MaxDurationSeconds s.")
}

if ($buildDelta.DeltaPercent -gt $MaxBuildGrowthPercent) {
    $warnings.Add("Croissance du BUILD_DIR elevee: {0:N2}%." -f $buildDelta.DeltaPercent)
}

if ($afterBuildDir.PathsOverLimit.Count -gt 0) {
    $warnings.Add("Chemins trop longs detectes sous BUILD_DIR: $($afterBuildDir.PathsOverLimit.Count).")
}

if ($diskAfter.FreeBytes -ge 0) {
    $freeMb = [math]::Round($diskAfter.FreeBytes / 1MB, 2)
    if ($freeMb -lt $MinFreeSpaceMB) {
        $alerts.Add("Espace disque faible sur $($diskAfter.Root): $freeMb MB < $MinFreeSpaceMB MB.")
    }
}

$baseline = Get-BaselineData -Path $BaselinePath
$baselineMessage = ""
if ($baseline -and $firmwareInfo.Exists -and $baseline.firmwareBinBytes) {
    $baselineSize = [double]$baseline.firmwareBinBytes
    if ($baselineSize -gt 0) {
        $deltaBaselinePercent = (($firmwareInfo.SizeBytes - $baselineSize) / $baselineSize) * 100.0
        $baselineMessage = "Baseline firmware.bin: {0:N0} -> {1:N0} bytes ({2:N2}%)." -f $baselineSize, $firmwareInfo.SizeBytes, $deltaBaselinePercent
        if ([math]::Abs($deltaBaselinePercent) -gt $MaxBaselineDeltaPercent) {
            $warnings.Add("Ecart firmware.bin vs baseline superieur a {0:N2}%." -f $MaxBaselineDeltaPercent)
        }
    }
}

if ($UpdateBaseline -and $firmwareInfo.Exists -and -not [string]::IsNullOrWhiteSpace($BaselinePath)) {
    Save-BaselineData -Path $BaselinePath -ProjectRootPath $resolvedProjectRoot -EnvName $Environment -FirmwareSize $firmwareInfo.SizeBytes
    $warnings.Add("Baseline mis a jour: $BaselinePath")
}

$pioStats = @()
if ($ParsePioStats -and $pioOutputLines.Count -gt 0) {
    $pioStats = $pioOutputLines | Where-Object {
        $_ -match "RAM:\s*\[" -or $_ -match "Flash:\s*\[" -or $_ -match "PROGRAM:\s*\[" -or $_ -match "DATA:\s*\["
    }
}

$reportLines = New-Object System.Collections.Generic.List[string]
$reportLines.Add("=== PIO BUILD AUDIT REPORT ===")
$reportLines.Add("Date UTC: $([DateTime]::UtcNow.ToString('yyyy-MM-dd HH:mm:ss'))")
$reportLines.Add("ProjectRoot: $resolvedProjectRoot")
$reportLines.Add("Environment: $Environment")
$reportLines.Add("Mode: $(if ($ScanOnly) { 'scan-only' } else { 'run+scan' })")
$reportLines.Add("BUILD_DIR: $buildDir")
$reportLines.Add("LIBDEPS_DIR: $libdepsDir")
$reportLines.Add("")

$reportLines.Add("[RESUME]")
$reportLines.Add("pioExitCode=$pioExitCode")
$reportLines.Add("durationSeconds=$durationSeconds")
$reportLines.Add("alerts=$($alerts.Count)")
$reportLines.Add("warnings=$($warnings.Count)")
$reportLines.Add("")

$reportLines.Add("[METRIQUES]")
$reportLines.Add("buildDir.before.bytes=$($beforeBuildDir.TotalBytes)")
$reportLines.Add("buildDir.after.bytes=$($afterBuildDir.TotalBytes)")
$reportLines.Add("buildDir.delta.bytes=$($buildDelta.DeltaBytes)")
$reportLines.Add("buildDir.delta.percent={0:N2}" -f $buildDelta.DeltaPercent)
$reportLines.Add("buildDir.before.files=$($beforeBuildDir.FileCount)")
$reportLines.Add("buildDir.after.files=$($afterBuildDir.FileCount)")
$reportLines.Add("buildDir.delta.files=$($buildDelta.DeltaFiles)")
$reportLines.Add("buildDir.longestPathLength=$($afterBuildDir.LongestPathLength)")
$reportLines.Add("libdeps.before.bytes=$($beforeLibdeps.TotalBytes)")
$reportLines.Add("libdeps.after.bytes=$($afterLibdeps.TotalBytes)")
$reportLines.Add("libdeps.delta.bytes=$($libdepsDelta.DeltaBytes)")
$reportLines.Add("libdeps.delta.percent={0:N2}" -f $libdepsDelta.DeltaPercent)
$reportLines.Add("disk.free.before.bytes=$($diskBefore.FreeBytes)")
$reportLines.Add("disk.free.after.bytes=$($diskAfter.FreeBytes)")
$reportLines.Add("")

$reportLines.Add("[ARTEFACTS]")
$reportLines.Add("firmware.bin.exists=$($firmwareInfo.Exists)")
$reportLines.Add("firmware.bin.sizeBytes=$($firmwareInfo.SizeBytes)")
$reportLines.Add("firmware.elf.exists=$($elfInfo.Exists)")
$reportLines.Add("firmware.elf.sizeBytes=$($elfInfo.SizeBytes)")
$reportLines.Add("littlefs.bin.exists=$($littlefsInfo.Exists)")
$reportLines.Add("littlefs.bin.sizeBytes=$($littlefsInfo.SizeBytes)")
if (-not [string]::IsNullOrWhiteSpace($baselineMessage)) {
    $reportLines.Add($baselineMessage)
}
$reportLines.Add("")

$reportLines.Add("[ALERTES]")
if ($alerts.Count -eq 0) {
    $reportLines.Add("Aucune alerte critique.")
} else {
    foreach ($alert in $alerts) {
        $reportLines.Add("- $alert")
    }
}
$reportLines.Add("")

$reportLines.Add("[WARNINGS]")
if ($warnings.Count -eq 0) {
    $reportLines.Add("Aucun warning.")
} else {
    foreach ($warning in $warnings) {
        $reportLines.Add("- $warning")
    }
}
$reportLines.Add("")

if ($afterBuildDir.LongestPath) {
    $reportLines.Add("[CHEMIN_LE_PLUS_LONG]")
    $reportLines.Add($afterBuildDir.LongestPath)
    $reportLines.Add("")
}

if ($afterBuildDir.PathsOverLimit.Count -gt 0) {
    $reportLines.Add("[CHEMINS_SUPERIEURS_A_LA_LIMITE]")
    $maxDisplay = [Math]::Min(10, $afterBuildDir.PathsOverLimit.Count)
    for ($i = 0; $i -lt $maxDisplay; $i++) {
        $reportLines.Add("- " + (Get-SafeRelativePath -FullPath $afterBuildDir.PathsOverLimit[$i] -RootPath $resolvedProjectRoot))
    }
    if ($afterBuildDir.PathsOverLimit.Count -gt $maxDisplay) {
        $remaining = $afterBuildDir.PathsOverLimit.Count - $maxDisplay
        $reportLines.Add("- ... $remaining chemins supplementaires non affiches")
    }
    $reportLines.Add("")
}

if ($ParsePioStats -and $pioStats.Count -gt 0) {
    $reportLines.Add("[PIO_STATS]")
    foreach ($line in $pioStats) {
        $reportLines.Add($line)
    }
    $reportLines.Add("")
}

$reportText = $reportLines -join [Environment]::NewLine

if ([string]::IsNullOrWhiteSpace($OutFile)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutFile = Join-Path $resolvedProjectRoot ("build-audit-" + $Environment + "-" + $stamp + ".txt")
}

$reportText | Set-Content -LiteralPath $OutFile -Encoding UTF8

Write-Host ""
Write-Host "=== BUILD AUDIT ===" -ForegroundColor Cyan
Write-Host "Rapport: $OutFile"
Write-Host "BUILD_DIR taille apres: $(Format-Bytes -Value $afterBuildDir.TotalBytes) ($($afterBuildDir.FileCount) fichiers)"
Write-Host "LIBDEPS taille apres: $(Format-Bytes -Value $afterLibdeps.TotalBytes) ($($afterLibdeps.FileCount) fichiers)"
if (-not $ScanOnly) {
    Write-Host "Duree build: $durationSeconds s, code retour pio: $pioExitCode"
}
Write-Host "Alertes: $($alerts.Count), warnings: $($warnings.Count)"

if ($tempPioLog -and (Test-Path -LiteralPath $tempPioLog)) {
    Remove-Item -LiteralPath $tempPioLog -Force -ErrorAction SilentlyContinue
}

if ($FailOnAnomaly -and ($alerts.Count -gt 0 -or $warnings.Count -gt 0)) {
    exit 2
}

if (-not $ScanOnly -and $pioExitCode -ne 0) {
    exit $pioExitCode
}
exit 0
