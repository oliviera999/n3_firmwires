# =============================================================================
# Script de publication OTA distant - FFP5CS
# =============================================================================
# Copie les firmware.bin et littlefs.bin compilés vers ffp3/ota/, met à jour
# metadata.json (firmware + filesystem), puis commit + push dans le dépôt ffp3.
#
# Prérequis: build déjà effectué pour les envs ciblés (pio run -e wroom-prod, etc.)
#            ou utiliser -Build pour compiler avant copie.
#            Pour le filesystem: pio run -e <env> -t buildfs ou -BuildFs.
#
# Usage:
#   .\scripts\publish_ota.ps1
#   .\scripts\publish_ota.ps1 -Envs "wroom-prod","wroom-beta","wroom-s3-prod","wroom-s3-test"
#   .\scripts\publish_ota.ps1 -DryRun
#   .\scripts\publish_ota.ps1 -Build
#   .\scripts\publish_ota.ps1 -Build -BuildFs
# =============================================================================

param(
    [string[]]$Envs = @("wroom-prod", "wroom-beta", "wroom-s3-prod", "wroom-s3-test"),
    [switch]$SkipCommit,
    [switch]$DryRun,
    [switch]$Build,
    [switch]$BuildFs,
    [switch]$SkipValidate,
    [bool]$IncludeFs = $true,
    [string]$OtaBaseUrl = "https://iot.olution.info/ffp3/ota"
)
# Validation des tailles (firmware/fs <= tailles partition) activée par défaut ; -SkipValidate pour désactiver.

$ErrorActionPreference = "Stop"

# Mapping env PlatformIO -> (sous-dossier OTA, canal, clé metadata, tailles partition)
# MetadataKey : clé dans channels.<canal> (ex. esp32-wroom, esp32-s3). Aligné firmware ota_config.h / getOTAFolder().
# AppSize/FsSize : tailles max partition (partitions_esp32_wroom_ota_fs_medium.csv, partitions_esp32_s3_test.csv)
$EnvToOta = @{
    "wroom-prod"     = @{ Subfolder = "esp32-wroom";      Channel = "prod"; AppSize = 1744896; FsSize = 720896 }   # 0x1A0000, 0x0B0000
    "wroom-beta"      = @{ Subfolder = "esp32-wroom-beta"; Channel = "test"; MetadataKey = "esp32-wroom"; AppSize = 1744896; FsSize = 720896 }
    "wroom-s3-prod"   = @{ Subfolder = "esp32-s3";         Channel = "prod"; MetadataKey = "esp32-s3"; AppSize = 7307264; FsSize = 2097152 }  # 0x6F8000, 0x200000
    "wroom-s3-test"   = @{ Subfolder = "esp32-s3-test";    Channel = "test"; MetadataKey = "esp32-s3"; AppSize = 7307264; FsSize = 2097152 }
}

# -----------------------------------------------------------------------------
# Vérifications
# -----------------------------------------------------------------------------
if (-not (Test-Path "platformio.ini")) {
    Write-Host "Erreur: platformio.ini introuvable. Executez depuis la racine du projet." -ForegroundColor Red
    exit 1
}
if (-not (Test-Path "ffp3/ota")) {
    Write-Host "Erreur: ffp3/ota/ introuvable. Verifiez que le sous-module ffp3 est initialise." -ForegroundColor Red
    exit 1
}
if (-not (Test-Path "ffp3/.git")) {
    Write-Host "Erreur: ffp3/.git introuvable. Le sous-module ffp3 doit etre initialise." -ForegroundColor Red
    exit 1
}
if (-not (Test-Path "include/config.h")) {
    Write-Host "Erreur: include/config.h introuvable." -ForegroundColor Red
    exit 1
}

# -----------------------------------------------------------------------------
# Version firmware : config.h = fallback ; par env = .pio/build/<env>/version.txt (écrit au build)
# -----------------------------------------------------------------------------
$configContent = Get-Content -Path "include/config.h" -Raw
if ($configContent -notmatch 'VERSION\s*=\s*"([^"]+)"') {
    Write-Host "Erreur: impossible d'extraire VERSION depuis include/config.h" -ForegroundColor Red
    exit 1
}
$configVersion = $Matches[1]
Write-Host "Version reference (config.h): $configVersion" -ForegroundColor Cyan
Write-Host "Version par env: .pio/build/<env>/version.txt si present, sinon config.h" -ForegroundColor Gray

# Normaliser l'URL de base (sans slash final)
$OtaBaseUrl = $OtaBaseUrl.TrimEnd('/')

# -----------------------------------------------------------------------------
# Build optionnel (firmware)
# -----------------------------------------------------------------------------
if ($Build) {
    Write-Host "Build firmware des envs: $($Envs -join ', ')" -ForegroundColor Yellow
    $buildJobs = if ($env:OS -eq "Windows_NT") { @("-j", "1") } else { @() }
    foreach ($e in $Envs) {
        $envPath = ".pio/build/$e/firmware.bin"
        if (-not (Test-Path $envPath)) {
            Write-Host "  Compilation $e..." -ForegroundColor Gray
            pio run -e $e @buildJobs
            if ($LASTEXITCODE -ne 0) {
                Write-Host "Erreur: build $e a echoue." -ForegroundColor Red
                exit 1
            }
        }
    }
}

# -----------------------------------------------------------------------------
# Build optionnel (filesystem LittleFS)
# -----------------------------------------------------------------------------
if ($BuildFs -and $IncludeFs) {
    Write-Host "Build filesystem des envs: $($Envs -join ', ')" -ForegroundColor Yellow
    $buildJobs = if ($env:OS -eq "Windows_NT") { @("-j", "1") } else { @() }
    foreach ($e in $Envs) {
        $mapping = $EnvToOta[$e]
        if (-not $mapping) { continue }
        $fsPath = ".pio/build/$e/littlefs.bin"
        if (-not (Test-Path $fsPath)) {
            Write-Host "  Build filesystem $e..." -ForegroundColor Gray
            pio run -e $e -t buildfs @buildJobs
            if ($LASTEXITCODE -ne 0) {
                Write-Host "Erreur: build filesystem $e a echoue." -ForegroundColor Red
                exit 1
            }
        }
    }
}

# -----------------------------------------------------------------------------
# Copie des binaires et collecte des infos pour metadata
# -----------------------------------------------------------------------------
$published = @()
$artifacts = @()  # @( @{ Channel; Subfolder; Version; BinUrl; Size; Md5 } )

foreach ($envName in $Envs) {
    $mapping = $EnvToOta[$envName]
    if (-not $mapping) {
        Write-Host "Avertissement: env '$envName' non mappe, ignore." -ForegroundColor Yellow
        continue
    }
    $srcPath = ".pio/build/$envName/firmware.bin"
    if (-not (Test-Path $srcPath)) {
        Write-Host "Avertissement: $srcPath introuvable, ignore." -ForegroundColor Yellow
        continue
    }
    # Version de ce build : version.txt (écrit par pio_write_build_version.py au build) ou config.h
    $versionFile = ".pio/build/$envName/version.txt"
    $artifactVersion = $configVersion
    if (Test-Path $versionFile) {
        $artifactVersion = (Get-Content -Path $versionFile -Raw).Trim()
        if ([string]::IsNullOrWhiteSpace($artifactVersion)) { $artifactVersion = $configVersion }
    }
    $subfolder = $mapping.Subfolder
    $channel = $mapping.Channel
    $destDir = "ffp3/ota/$subfolder"
    $destPath = "$destDir/firmware.bin"
    if (-not (Test-Path $destDir)) {
        New-Item -ItemType Directory -Path $destDir -Force | Out-Null
        Write-Host "Cree: $destDir" -ForegroundColor Gray
    }
    Copy-Item -Path $srcPath -Destination $destPath -Force
    $size = (Get-Item $destPath).Length
    $hash = Get-FileHash -Path $destPath -Algorithm MD5
    $md5 = $hash.Hash.ToLowerInvariant()
    $binUrl = "$OtaBaseUrl/$subfolder/firmware.bin"
    $metaKey = if ($mapping.MetadataKey) { $mapping.MetadataKey } else { $subfolder }

    # Filesystem LittleFS (optionnel)
    $fsUrl = $null
    $fsSize = 0
    $fsMd5 = ""
    if ($IncludeFs) {
        $fsSrcPath = ".pio/build/$envName/littlefs.bin"
        if (Test-Path $fsSrcPath) {
            $fsDestPath = "$destDir/littlefs.bin"
            Copy-Item -Path $fsSrcPath -Destination $fsDestPath -Force
            $fsSize = (Get-Item $fsDestPath).Length
            $fsHash = Get-FileHash -Path $fsDestPath -Algorithm MD5
            $fsMd5 = $fsHash.Hash.ToLowerInvariant()
            $fsUrl = "$OtaBaseUrl/$subfolder/littlefs.bin"
            Write-Host "  + littlefs.bin -> $fsDestPath (size=$fsSize)" -ForegroundColor Gray
        } else {
            Write-Host "  Avertissement: littlefs.bin absent pour $envName (pio run -e $envName -t buildfs)" -ForegroundColor Yellow
        }
    }

    $appMax = if ($mapping.AppSize) { $mapping.AppSize } else { 1744896 }
    $fsMax  = if ($mapping.FsSize)  { $mapping.FsSize }  else { 720896 }
    $artifacts += @{
        Channel          = $channel
        Subfolder        = $subfolder
        MetadataKey      = $metaKey
        Version          = $artifactVersion
        BinUrl           = $binUrl
        Size             = $size
        Md5              = $md5
        FsUrl            = $fsUrl
        FsSize           = $fsSize
        FsMd5            = $fsMd5
        AppPartitionSize = $appMax
        FsPartitionSize  = $fsMax
    }
    $published += $envName
    Write-Host "Copie: $envName -> $destPath (version=$artifactVersion, size=$size, md5=$md5)" -ForegroundColor Green
}

# -----------------------------------------------------------------------------
# Validation des tailles (partitions par env) — exécutée par défaut, -SkipValidate pour ignorer
# -----------------------------------------------------------------------------
if (-not $SkipValidate) {
    foreach ($a in $artifacts) {
        $appMax = $a.AppPartitionSize
        $fsMax  = $a.FsPartitionSize
        if ($a.Size -gt $appMax) {
            Write-Host "Erreur validation: firmware $($a.Subfolder) taille $($a.Size) > partition app $appMax" -ForegroundColor Red
            exit 1
        }
        if ($a.FsSize -gt 0 -and $a.FsSize -gt $fsMax) {
            Write-Host "Erreur validation: filesystem $($a.Subfolder) taille $($a.FsSize) > partition fs $fsMax" -ForegroundColor Red
            exit 1
        }
    }
    Write-Host "Validation tailles OK (par env: WROOM app 0x1A0000/fs 0x0B0000, S3 app 0x6F8000/fs 0x200000)" -ForegroundColor Green
}

if ($artifacts.Count -eq 0) {
    Write-Host "Erreur: aucun binaire publie. Compilez les envs ou verifiez -Envs." -ForegroundColor Red
    exit 1
}

# -----------------------------------------------------------------------------
# Mise à jour metadata.json
# -----------------------------------------------------------------------------
$metaPath = "ffp3/ota/metadata.json"
$meta = $null
if (Test-Path $metaPath) {
    try {
        $meta = Get-Content -Path $metaPath -Raw -Encoding UTF8 | ConvertFrom-Json
    } catch {
        Write-Host "Erreur: metadata.json illisible: $_" -ForegroundColor Red
        exit 1
    }
} else {
    $meta = [PSCustomObject]@{
        version = $configVersion
        bin_url = ""
        size    = 0
        md5     = ""
        channels = [PSCustomObject]@{}
    }
}

# S'assurer que channels existe
if (-not $meta.PSObject.Properties["channels"]) {
    $meta | Add-Member -NotePropertyName "channels" -NotePropertyValue ([PSCustomObject]@{}) -Force
}
# S'assurer que channels.prod et channels.test existent
if (-not $meta.channels.PSObject.Properties["prod"]) {
    $meta.channels | Add-Member -NotePropertyName "prod" -NotePropertyValue ([PSCustomObject]@{}) -Force
}
if (-not $meta.channels.PSObject.Properties["test"]) {
    $meta.channels | Add-Member -NotePropertyName "test" -NotePropertyValue ([PSCustomObject]@{}) -Force
}

# Ecrire chaque artifact dans son canal (prod ou test) sous la clé MetadataKey
$prodDefaultEntry = $null
$testDefaultEntry = $null
foreach ($a in $artifacts) {
    $entryProps = @{
        version = $a.Version
        bin_url = $a.BinUrl
        size    = $a.Size
        md5     = $a.Md5
    }
    if ($a.FsUrl) {
        $entryProps["filesystem_url"] = $a.FsUrl
        $entryProps["filesystem_size"] = $a.FsSize
        $entryProps["filesystem_md5"] = $a.FsMd5
    }
    $entry = [PSCustomObject]$entryProps
    $channelObj = $meta.channels.($a.Channel)
    $key = $a.MetadataKey
    if (-not $channelObj.PSObject.Properties[$key]) {
        $channelObj | Add-Member -NotePropertyName $key -NotePropertyValue $entry -Force
    } else {
        $channelObj.$key = $entry
    }
    if ($a.Channel -eq "prod") {
        if ($key -eq "esp32-wroom") { $prodDefaultEntry = $entry }
        elseif (-not $prodDefaultEntry) { $prodDefaultEntry = $entry }
    }
    if ($a.Channel -eq "test") {
        if ($key -eq "esp32-wroom") { $testDefaultEntry = $entry }
        elseif (-not $testDefaultEntry) { $testDefaultEntry = $entry }
    }
}

# default par canal
if ($prodDefaultEntry) {
    if (-not $meta.channels.prod.PSObject.Properties["default"]) {
        $meta.channels.prod | Add-Member -NotePropertyName "default" -NotePropertyValue $prodDefaultEntry -Force
    } else {
        $meta.channels.prod.default = $prodDefaultEntry
    }
}
if ($testDefaultEntry) {
    if (-not $meta.channels.test.PSObject.Properties["default"]) {
        $meta.channels.test | Add-Member -NotePropertyName "default" -NotePropertyValue $testDefaultEntry -Force
    } else {
        $meta.channels.test.default = $testDefaultEntry
    }
}

# Racine legacy (version, bin_url, size, md5) = default prod si present, sinon premier artifact
$legacyEntry = $prodDefaultEntry
if (-not $legacyEntry -and $artifacts.Count -gt 0) {
    $first = $artifacts[0]
    $legacyEntry = [PSCustomObject]@{ version = $first.Version; bin_url = $first.BinUrl; size = $first.Size; md5 = $first.Md5 }
}
if ($legacyEntry) {
    $meta.version = $legacyEntry.version
    $meta.bin_url = $legacyEntry.bin_url
    $meta.size    = $legacyEntry.size
    $meta.md5     = $legacyEntry.md5
}

# Ecriture JSON (Depth 5 pour channels.prod.esp32-wroom etc.)
$formatted = $meta | ConvertTo-Json -Depth 5
[System.IO.File]::WriteAllText((Join-Path $PWD $metaPath), $formatted, [System.Text.UTF8Encoding]::new($false))
Write-Host "Metadata mis a jour: $metaPath" -ForegroundColor Green

# -----------------------------------------------------------------------------
# Git commit + push dans ffp3
# -----------------------------------------------------------------------------
if ($SkipCommit -or $DryRun) {
    Write-Host "SkipCommit/DryRun: pas de commit ni push." -ForegroundColor Gray
    if ($DryRun) {
        Write-Host "Fichiers modifies dans ffp3/ota (git status):" -ForegroundColor Cyan
        Push-Location ffp3
        git status --short ota/
        Pop-Location
    }
    Write-Host "Termine (dry run)." -ForegroundColor Cyan
    exit 0
}

Push-Location ffp3
try {
    git add ota/
    $status = git status --porcelain ota/
    if ([string]::IsNullOrWhiteSpace($status)) {
        Write-Host "Aucun changement dans ffp3/ota/, rien a committer." -ForegroundColor Gray
    } else {
        $versionList = ($artifacts | ForEach-Object { "$($_.Subfolder)=$($_.Version)" }) -join ", "
        $commitMsg = "ota: publish $versionList"
        git commit -m $commitMsg
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Erreur: git commit a echoue." -ForegroundColor Red
            exit 1
        }
        git push
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Erreur: git push a echoue." -ForegroundColor Red
            exit 1
        }
        Write-Host "Commit et push ffp3 reussis." -ForegroundColor Green
    }
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "Rappel: le depot parent (ffp5cs) pointe vers une nouvelle ref du sous-module ffp3." -ForegroundColor Yellow
$versionsSummary = ($artifacts | ForEach-Object { $_.Version } | Select-Object -Unique) -join ", "
Write-Host "Pour versionner cette ref: git add ffp3 && git commit -m 'chore: update ffp3 ref (OTA $versionsSummary)'" -ForegroundColor Gray
Write-Host "Publication OTA terminee." -ForegroundColor Cyan
