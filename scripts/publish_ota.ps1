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
#   .\scripts\publish_ota.ps1 -Envs "wroom-prod","wroom-beta"
#   .\scripts\publish_ota.ps1 -DryRun
#   .\scripts\publish_ota.ps1 -Build
#   .\scripts\publish_ota.ps1 -Build -BuildFs
# =============================================================================

param(
    [string[]]$Envs = @("wroom-prod", "wroom-beta"),
    [switch]$SkipCommit,
    [switch]$DryRun,
    [switch]$Build,
    [switch]$BuildFs,
    [switch]$SkipValidate,
    [bool]$IncludeFs = $true,
    [string]$OtaBaseUrl = "https://iot.olution.info/ffp3/ota"
)
# Validation des tailles (firmware/fs <= tailles partition) activée par défaut ; -SkipValidate pour désactiver.

# Tailles partitions WROOM (partitions_esp32_wroom_ota_fs_medium.csv)
$SCRIPT:OTA_APP_PARTITION_SIZE = 1744896   # 0x1A0000
$SCRIPT:OTA_FS_PARTITION_SIZE = 720896     # 0x0B0000

$ErrorActionPreference = "Stop"

# Mapping env PlatformIO -> (sous-dossier OTA, canal, clé metadata optionnelle)
# MetadataKey : clé dans channels.<canal> (ex. wroom-beta cherche channels.test.esp32-wroom)
# S3 (wroom-s3-prod, wroom-s3-test) : à intégrer ultérieurement
$EnvToOta = @{
    "wroom-prod"       = @{ Subfolder = "esp32-wroom";      Channel = "prod" }
    # "wroom-s3-prod"   = @{ Subfolder = "esp32-s3";         Channel = "prod" }
    "wroom-beta"        = @{ Subfolder = "esp32-wroom-beta"; Channel = "test"; MetadataKey = "esp32-wroom" }
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
# Version firmware depuis include/config.h
# -----------------------------------------------------------------------------
$configContent = Get-Content -Path "include/config.h" -Raw
if ($configContent -notmatch 'VERSION\s*=\s*"([^"]+)"') {
    Write-Host "Erreur: impossible d'extraire VERSION depuis include/config.h" -ForegroundColor Red
    exit 1
}
$firmwareVersion = $Matches[1]
Write-Host "Version firmware: $firmwareVersion" -ForegroundColor Cyan

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

    $artifacts += @{
        Channel       = $channel
        Subfolder     = $subfolder
        MetadataKey   = $metaKey
        Version       = $firmwareVersion
        BinUrl        = $binUrl
        Size          = $size
        Md5           = $md5
        FsUrl         = $fsUrl
        FsSize        = $fsSize
        FsMd5         = $fsMd5
    }
    $published += $envName
    Write-Host "Copie: $envName -> $destPath (size=$size, md5=$md5)" -ForegroundColor Green
}

# -----------------------------------------------------------------------------
# Validation des tailles (partitions WROOM) — exécutée par défaut, -SkipValidate pour ignorer
# -----------------------------------------------------------------------------
if (-not $SkipValidate) {
    foreach ($a in $artifacts) {
        if ($a.Size -gt $SCRIPT:OTA_APP_PARTITION_SIZE) {
            Write-Host "Erreur validation: firmware $($a.Subfolder) taille $($a.Size) > partition app $SCRIPT:OTA_APP_PARTITION_SIZE" -ForegroundColor Red
            exit 1
        }
        if ($a.FsSize -gt 0 -and $a.FsSize -gt $SCRIPT:OTA_FS_PARTITION_SIZE) {
            Write-Host "Erreur validation: filesystem $($a.Subfolder) taille $($a.FsSize) > partition spiffs $SCRIPT:OTA_FS_PARTITION_SIZE" -ForegroundColor Red
            exit 1
        }
    }
    Write-Host "Validation tailles OK (firmware <= $SCRIPT:OTA_APP_PARTITION_SIZE, fs <= $SCRIPT:OTA_FS_PARTITION_SIZE)" -ForegroundColor Green
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
        version = $firmwareVersion
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
        $envList = $published -join ", "
        $commitMsg = "ota: publish firmware $firmwareVersion ($envList)"
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
Write-Host "Pour versionner cette ref: git add ffp3 && git commit -m 'chore: update ffp3 ref (OTA $firmwareVersion)'" -ForegroundColor Gray
Write-Host "Publication OTA terminee." -ForegroundColor Cyan
