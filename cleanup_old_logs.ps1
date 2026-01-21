# Script de nettoyage des anciens logs
# Supprime les logs obsolètes (versions < v11.130 ou >30 jours sauf références récentes)

Write-Host "=== NETTOYAGE DES LOGS OBSOLÈTES ===" -ForegroundColor Green
Write-Host "Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan
Write-Host ""

$deleted = 0
$totalSize = 0
$cutoffDate = (Get-Date).AddDays(-30)
$cutoffVersion = 11.130

# Liste explicite des logs obsolètes à supprimer (versions < v11.130)
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

Write-Host "1. Suppression des logs de versions obsolètes (< v11.130)..." -ForegroundColor Yellow
foreach ($file in $oldVersionLogs) {
    if (Test-Path $file) {
        $size = (Get-Item $file).Length
        Remove-Item $file -Force
        $deleted++
        $totalSize += $size
        Write-Host "  Supprimé: $file ($([math]::Round($size/1KB, 2)) KB)" -ForegroundColor Gray
    }
}

Write-Host ""
Write-Host "2. Suppression des logs >30 jours (sauf références récentes)..." -ForegroundColor Yellow

# Supprimer les logs anciens (>30 jours) mais garder les plus récents par type
$oldLogs = Get-ChildItem -Path . -Filter "*.log" | Where-Object {
    $_.LastWriteTime -lt $cutoffDate -and 
    $_.Name -notmatch "tools\\"
} | Sort-Object LastWriteTime -Descending

# Garder les 5 plus récents de chaque type
$logTypes = @{}
foreach ($log in $oldLogs) {
    # Extraire le type de log (ex: "monitor_5min_wroom_test", "monitor_90s", etc.)
    if ($log.Name -match "^(monitor_[^_]+_[^_]+|monitor_[^_]+|monitor_unlimited|monitor_until_crash|monitor_v11\.|monitor_10min|monitor_15min|monitor_5min)") {
        $baseType = $matches[0]
        if (-not $logTypes.ContainsKey($baseType)) {
            $logTypes[$baseType] = @()
        }
        if ($logTypes[$baseType].Count -lt 5) {
            $logTypes[$baseType] += $log
        } else {
            # Supprimer ce log (plus de 5 références de ce type)
            $size = $log.Length
            Remove-Item $log.FullName -Force
            $deleted++
            $totalSize += $size
            Write-Host "  Supprimé: $($log.Name) ($([math]::Round($size/1KB, 2)) KB)" -ForegroundColor Gray
        }
    } else {
        # Log sans pattern connu, supprimer si >30 jours
        $size = $log.Length
        Remove-Item $log.FullName -Force
        $deleted++
        $totalSize += $size
        Write-Host "  Supprimé: $($log.Name) ($([math]::Round($size/1KB, 2)) KB)" -ForegroundColor Gray
    }
}

Write-Host ""
Write-Host "3. Suppression des fichiers .errors associés..." -ForegroundColor Yellow

$errorFiles = Get-ChildItem -Path . -Filter "*.errors" | Where-Object {
    $baseName = $_.BaseName -replace '\.log$', ''
    -not (Test-Path "$baseName.log")
}

foreach ($errorFile in $errorFiles) {
    $size = $errorFile.Length
    Remove-Item $errorFile.FullName -Force
    $deleted++
    $totalSize += $size
    Write-Host "  Supprimé: $($errorFile.Name) ($([math]::Round($size/1KB, 2)) KB)" -ForegroundColor Gray
}

Write-Host ""
Write-Host "=== RÉSUMÉ ===" -ForegroundColor Cyan
Write-Host "Total: $deleted fichier(s) supprimé(s)" -ForegroundColor Green
Write-Host "Espace libéré: $([math]::Round($totalSize/1MB, 2)) MB" -ForegroundColor Green