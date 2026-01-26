# =============================================================================
# Script de nettoyage des anciens logs (version améliorée)
# =============================================================================
# Description:
#   Supprime les fichiers de logs obsolètes du projet selon plusieurs critères :
#   - Logs vides (0 octets)
#   - Fichiers .errors orphelins/vides
#   - Logs très volumineux (> 10 MB et > 7 jours, ou > 50 MB sauf si < 3 jours)
#   - Logs de versions < v11.130 (liste explicite)
#   - Logs > 30 jours (en gardant les 10 plus récents par type)
#   - Fichiers d'analyse obsolètes
#
# Prérequis:
#   - PowerShell 5.1+ (Windows)
#
# Usage:
#   .\cleanup_old_logs.ps1 [-WhatIf]
#
# Paramètres:
#   -WhatIf    Mode dry-run : affiche ce qui serait supprimé sans supprimer
#
# Notes:
#   - Le script est sûr : il ne supprime que les logs vraiment obsolètes
#   - Les logs récents (< 30 jours) sont conservés
#   - Affiche un résumé détaillé par catégorie
# =============================================================================

param(
    [switch]$WhatIf
)

Write-Host "=== NETTOYAGE DES LOGS OBSOLÈTES ===" -ForegroundColor Green
Write-Host "Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan
if ($WhatIf) {
    Write-Host "Mode: DRY-RUN (aucune suppression réelle)" -ForegroundColor Yellow
}
Write-Host ""

# Compteurs par catégorie
$stats = @{
    EmptyLogs = @{ Count = 0; Size = 0 }
    EmptyErrors = @{ Count = 0; Size = 0 }
    OrphanErrors = @{ Count = 0; Size = 0 }
    LargeLogs = @{ Count = 0; Size = 0 }
    OldVersions = @{ Count = 0; Size = 0 }
    OldByType = @{ Count = 0; Size = 0 }
    OldAnalysis = @{ Count = 0; Size = 0 }
}

$cutoffDate = (Get-Date).AddDays(-30)
$cutoffDateLarge = (Get-Date).AddDays(-7)
$cutoffDateVeryLarge = (Get-Date).AddDays(-3)

# Fonction helper pour supprimer un fichier
function Remove-LogFile {
    param(
        [string]$FilePath,
        [string]$Category
    )
    
    if (Test-Path $FilePath) {
        $file = Get-Item $FilePath
        $size = $file.Length
        
        if ($WhatIf) {
            Write-Host "  [DRY-RUN] Supprimerait: $($file.Name) ($([math]::Round($size/1KB, 2)) KB)" -ForegroundColor DarkGray
        } else {
            Remove-Item $FilePath -Force -ErrorAction SilentlyContinue
            Write-Host "  Supprimé: $($file.Name) ($([math]::Round($size/1KB, 2)) KB)" -ForegroundColor Gray
        }
        
        $stats[$Category].Count++
        $stats[$Category].Size += $size
        return $true
    }
    return $false
}

# =============================================================================
# 1. Suppression des logs vides (0 octets)
# =============================================================================
Write-Host "1. Suppression des logs vides (0 octets)..." -ForegroundColor Yellow

$emptyLogs = Get-ChildItem -Path . -Filter "*.log" | Where-Object { $_.Length -eq 0 }
foreach ($log in $emptyLogs) {
    Remove-LogFile -FilePath $log.FullName -Category "EmptyLogs"
}

# =============================================================================
# 2. Suppression des fichiers .errors vides et orphelins
# =============================================================================
Write-Host ""
Write-Host "2. Suppression des fichiers .errors vides et orphelins..." -ForegroundColor Yellow

$errorFiles = Get-ChildItem -Path . -Filter "*.errors"
foreach ($errorFile in $errorFiles) {
    # Vérifier si vide
    if ($errorFile.Length -eq 0) {
        Remove-LogFile -FilePath $errorFile.FullName -Category "EmptyErrors"
        continue
    }
    
    # Vérifier si orphelin (pas de fichier .log associé)
    $baseName = $errorFile.BaseName -replace '\.log$', ''
    $logFile = "$baseName.log"
    if (-not (Test-Path $logFile)) {
        Remove-LogFile -FilePath $errorFile.FullName -Category "OrphanErrors"
    }
}

# =============================================================================
# 3. Suppression des logs très volumineux
# =============================================================================
Write-Host ""
Write-Host "3. Suppression des logs très volumineux..." -ForegroundColor Yellow

$largeLogs = Get-ChildItem -Path . -Filter "*.log" | Where-Object {
    $_.Length -gt 10MB -and $_.LastWriteTime -lt $cutoffDateLarge
} | Sort-Object Length -Descending

foreach ($log in $largeLogs) {
    # Logs > 50 MB: supprimer immédiatement sauf si < 3 jours
    if ($log.Length -gt 50MB -and $log.LastWriteTime -lt $cutoffDateVeryLarge) {
        Remove-LogFile -FilePath $log.FullName -Category "LargeLogs"
        # Supprimer aussi le fichier .errors associé s'il existe
        $errorFile = "$($log.FullName).errors"
        if (Test-Path $errorFile) {
            Remove-LogFile -FilePath $errorFile -Category "LargeLogs"
        }
    }
    # Logs > 10 MB et > 7 jours
    elseif ($log.Length -gt 10MB -and $log.LastWriteTime -lt $cutoffDateLarge) {
        Remove-LogFile -FilePath $log.FullName -Category "LargeLogs"
        # Supprimer aussi le fichier .errors associé s'il existe
        $errorFile = "$($log.FullName).errors"
        if (Test-Path $errorFile) {
            Remove-LogFile -FilePath $errorFile -Category "LargeLogs"
        }
    }
}

# =============================================================================
# 4. Suppression des logs de versions obsolètes (< v11.130)
# =============================================================================
Write-Host ""
Write-Host "4. Suppression des logs de versions obsolètes (< v11.130)..." -ForegroundColor Yellow

$oldVersionLogs = @(
    'monitor_90s_v11.58_2025-10-16_20-39-34.log',
    'monitor_90s_v11.58_2025-10-16_20-41-33.log',
    'monitor_simple_v11.58_2025-10-16_20-52-40.log',
    'monitor_5min_v11.63_2025-10-16_21-35-58.log',
    'monitor_v11.69_direct.log',
    'monitor_v11.69_simple.log',
    'monitor_90s_v11.70_direct.log',
    'monitor_90s_v11.72.log',
    'monitor_90s_v11.74.log',
    'monitor_90s_v11.74_2.log',
    'monitor_90s_v11.74_3.log',
    'monitor_90s_v11.75_flags.log',
    'monitor_10min_v11.77_direct_2025-10-19_11-05-46.log',
    'monitor_90s.log',
    'error_monitor_90s_v11.70_post_flash_2025-10-18_14-35-32.log',
    'monitor_90s_v11.81_post_flash_2025-10-20_20-44-15.log',
    'monitor_90s_v11.79_post_flash_2025-10-20_15-28-46.log',
    'monitor_90s_v11.70_post_flash_2025-10-18_14-35-32.log',
    'monitor_90s_v11.79_post_flash_2025-10-30_16-07-34.log',
    'monitor_90s_v11.118_simplification_2025-11-13_18-55-46.log',
    'monitor_90s_v11.118_simplification_2026-01-10_10-04-05.log',
    'monitor_90s_v11.119_2026-01-10_10-04-53.log',
    'monitor_90s_v11.120_2026-01-10_10-18-29.log',
    'monitor_90s_v11.120_2026-01-10_10-26-30.log',
    'monitor_90s_v11.120_2026-01-10_10-31-15.log',
    'monitor_90s_v11.121_2026-01-10_13-38-28.log',
    'monitor_90s_v11.121_2026-01-10_13-44-49.log',
    'monitor_90s_v11.121_2026-01-10_13-50-36.log',
    'monitor_90s_v11.121_2026-01-10_14-43-26.log',
    'monitor_90s_v11.122_2026-01-10_14-56-31.log',
    'monitor_90s_v11.123_2026-01-10_15-04-25.log',
    'monitor_90s_v11.123_nofooter_2026-01-10_15-10-16.log',
    'monitor_15min_panic_2025-11-13_13-10-40.log',
    'monitor_15min_panic_2025-11-13_13-13-45.log'
)

foreach ($file in $oldVersionLogs) {
    Remove-LogFile -FilePath $file -Category "OldVersions"
    # Supprimer aussi le fichier .errors associé s'il existe
    $errorFile = "$file.errors"
    if (Test-Path $errorFile) {
        Remove-LogFile -FilePath $errorFile -Category "OldVersions"
    }
}

# =============================================================================
# 5. Suppression des logs anciens par type (amélioré)
# =============================================================================
Write-Host ""
Write-Host "5. Suppression des logs >30 jours (en gardant les 10 plus récents par type)..." -ForegroundColor Yellow

# Fonction améliorée pour extraire le type de log
function Get-LogType {
    param([string]$FileName)
    
    # Patterns améliorés pour détecter les types
    if ($FileName -match '^monitor_(\d+min|15min|10min|5min|90s|unlimited|until_crash|v11\.\d+)') {
        return $matches[1]
    }
    elseif ($FileName -match '^monitor_(wroom_test|port\d+|smtp_|test_mail|heap\d+k)') {
        return $matches[1]
    }
    elseif ($FileName -match '^monitor_(\w+_v11\.\d+|\w+_\d{4}-\d{2}-\d{2})') {
        return $matches[1]
    }
    elseif ($FileName -match '^monitor_(\w+_debug|\w+_test|\w+_final|\w+_check)') {
        return $matches[1]
    }
    else {
        # Extraire un préfixe générique
        if ($FileName -match '^monitor_([^_]+)') {
            return $matches[1]
        }
    }
    return "other"
}

# Supprimer les logs anciens (>30 jours) mais garder les plus récents par type
$oldLogs = Get-ChildItem -Path . -Filter "*.log" | Where-Object {
    $_.LastWriteTime -lt $cutoffDate -and 
    $_.Name -notmatch "tools\\" -and
    $_.Length -gt 0  # Exclure les logs vides déjà traités
} | Sort-Object LastWriteTime -Descending

# Grouper par type et garder les 10 plus récents de chaque type
$logTypes = @{}
foreach ($log in $oldLogs) {
    $logType = Get-LogType -FileName $log.Name
    
    if (-not $logTypes.ContainsKey($logType)) {
        $logTypes[$logType] = @()
    }
    
    if ($logTypes[$logType].Count -lt 10) {
        $logTypes[$logType] += $log
    } else {
        # Supprimer ce log (plus de 10 références de ce type)
        Remove-LogFile -FilePath $log.FullName -Category "OldByType"
        # Supprimer aussi le fichier .errors associé s'il existe
        $errorFile = "$($log.FullName).errors"
        if (Test-Path $errorFile) {
            Remove-LogFile -FilePath $errorFile -Category "OldByType"
        }
    }
}

# =============================================================================
# 6. Suppression des fichiers d'analyse obsolètes
# =============================================================================
Write-Host ""
Write-Host "6. Suppression des fichiers d'analyse obsolètes..." -ForegroundColor Yellow

$analysisFiles = Get-ChildItem -Path . -Filter "*_analysis.txt" | Sort-Object LastWriteTime -Descending

# Grouper les analyses par type de log associé
$analysisByType = @{}
foreach ($analysis in $analysisFiles) {
    # Extraire le nom du log associé (enlever _analysis.txt)
    $logName = $analysis.Name -replace '_analysis\.txt$', '.log'
    $logType = Get-LogType -FileName $logName
    
    if (-not $analysisByType.ContainsKey($logType)) {
        $analysisByType[$logType] = @()
    }
    $analysisByType[$logType] += $analysis
}

# Supprimer les analyses orphelines (pas de log associé)
foreach ($analysis in $analysisFiles) {
    $logName = $analysis.Name -replace '_analysis\.txt$', '.log'
    if (-not (Test-Path $logName)) {
        Remove-LogFile -FilePath $analysis.FullName -Category "OldAnalysis"
    }
}

# Supprimer les analyses anciennes (garder les 5 plus récentes par type)
foreach ($logType in $analysisByType.Keys) {
    $analyses = $analysisByType[$logType] | Sort-Object LastWriteTime -Descending
    for ($i = 5; $i -lt $analyses.Count; $i++) {
        Remove-LogFile -FilePath $analyses[$i].FullName -Category "OldAnalysis"
    }
}

# =============================================================================
# 7. Rapport final détaillé
# =============================================================================
Write-Host ""
Write-Host "=== RÉSUMÉ DÉTAILLÉ ===" -ForegroundColor Cyan
Write-Host ""

$totalDeleted = 0
$totalSize = 0

$categories = @(
    @{ Name = "Logs vides"; Key = "EmptyLogs" },
    @{ Name = "Fichiers .errors vides"; Key = "EmptyErrors" },
    @{ Name = "Fichiers .errors orphelins"; Key = "OrphanErrors" },
    @{ Name = "Logs très volumineux"; Key = "LargeLogs" },
    @{ Name = "Logs versions obsolètes"; Key = "OldVersions" },
    @{ Name = "Logs anciens par type"; Key = "OldByType" },
    @{ Name = "Fichiers d'analyse obsolètes"; Key = "OldAnalysis" }
)

foreach ($cat in $categories) {
    $count = $stats[$cat.Key].Count
    $size = $stats[$cat.Key].Size
    $totalDeleted += $count
    $totalSize += $size
    
    if ($count -gt 0) {
        Write-Host "$($cat.Name):" -ForegroundColor Yellow -NoNewline
        Write-Host " $count fichier(s), $([math]::Round($size/1MB, 2)) MB" -ForegroundColor White
    }
}

Write-Host ""
Write-Host "=== TOTAL ===" -ForegroundColor Green
if ($WhatIf) {
    Write-Host "Fichiers qui seraient supprimés: $totalDeleted" -ForegroundColor Yellow
    Write-Host "Espace qui serait libéré: $([math]::Round($totalSize/1MB, 2)) MB" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Pour effectuer réellement le nettoyage, exécutez sans -WhatIf" -ForegroundColor Cyan
} else {
    Write-Host "Total fichiers supprimés: $totalDeleted" -ForegroundColor Green
    Write-Host "Espace libéré: $([math]::Round($totalSize/1MB, 2)) MB" -ForegroundColor Green
}
