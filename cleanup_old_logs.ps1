# Script de nettoyage des anciens logs
$filesToDelete = @(
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
    'esp32_logs_v11.116.txt'
)

$deleted = 0
$totalSize = 0

foreach ($file in $filesToDelete) {
    if (Test-Path $file) {
        $size = (Get-Item $file).Length
        Remove-Item $file -Force
        $deleted++
        $totalSize += $size
        Write-Host "Supprimé: $file ($([math]::Round($size/1MB, 2)) MB)" -ForegroundColor Green
    }
}

Write-Host ""
Write-Host "Total: $deleted fichier(s) supprimé(s), $([math]::Round($totalSize/1MB, 2)) MB libéré(s)" -ForegroundColor Cyan

