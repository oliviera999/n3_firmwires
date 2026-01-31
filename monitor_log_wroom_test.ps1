# Script simple de monitoring avec enregistrement des logs
# Usage: .\monitor_log_wroom_test.ps1 [durée_en_secondes]

param(
    [int]$Duration = 180  # Durée par défaut: 3 minutes
)

Write-Host "=== MONITORING WROOM-TEST - ENREGISTREMENT LOGS ===" -ForegroundColor Green
Write-Host "Durée: $Duration secondes ($([math]::Round($Duration/60, 1)) minutes)" -ForegroundColor Yellow
Write-Host "Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan
Write-Host ""

$logFile = "monitor_wroom_test_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"
Write-Host "📋 Fichier de log: $logFile" -ForegroundColor Cyan
Write-Host ""

# Port COM (détection automatique ou COM4 par défaut)
$comPort = "COM4"

Write-Host "🔌 Port série: $comPort" -ForegroundColor Gray

# Arrêter les processus qui pourraient bloquer le port COM
Write-Host "🔍 Vérification des processus bloquants..." -ForegroundColor Yellow
try {
    $blockingProcesses = Get-Process | Where-Object { 
        $_.ProcessName -like "*python*" -or 
        $_.ProcessName -like "*pio*" -or
        $_.MainWindowTitle -like "*monitor*" -or 
        $_.MainWindowTitle -like "*serial*" 
    }
    if ($blockingProcesses) {
        Write-Host "  Processus détectés: $($blockingProcesses.Count)" -ForegroundColor Yellow
        foreach ($proc in $blockingProcesses) {
            Write-Host "    - Arrêt: $($proc.ProcessName) (ID: $($proc.Id))" -ForegroundColor Gray
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        }
        Start-Sleep -Seconds 2
        Write-Host "  ✅ Processus arrêtés" -ForegroundColor Green
    } else {
        Write-Host "  ✅ Aucun processus bloquant détecté" -ForegroundColor Green
    }
} catch {
    Write-Host "  ⚠️ Erreur lors de la vérification: $($_.Exception.Message)" -ForegroundColor Yellow
}

Write-Host "📡 Démarrage du monitoring..." -ForegroundColor Cyan
Write-Host ""

try {
    # Lancer le monitoring avec redirection vers fichier
    $monitorProcess = Start-Process -FilePath "pio" -ArgumentList @("device", "monitor", "--port", $comPort, "--baud", "115200") -NoNewWindow -PassThru -RedirectStandardOutput $logFile -RedirectStandardError "$logFile.errors"
    
    Write-Host "Monitoring démarré (PID: $($monitorProcess.Id))" -ForegroundColor Green
    Write-Host "Enregistrement des logs dans: $logFile" -ForegroundColor Gray
    Write-Host ""
    
    # Attendre la durée spécifiée
    $endTime = (Get-Date).AddSeconds($Duration)
    $lastUpdate = 0
    
    while ((Get-Date) -lt $endTime) {
        $remaining = [math]::Round(($endTime - (Get-Date)).TotalSeconds)
        
        # Afficher le temps restant toutes les 30 secondes
        if ($remaining -le $lastUpdate - 30 -or $lastUpdate -eq 0) {
            $elapsed = $Duration - $remaining
            $minutes = [math]::Floor($elapsed / 60)
            $seconds = $elapsed % 60
            Write-Host "[$(Get-Date -Format 'HH:mm:ss')] Temps écoulé: ${minutes}m${seconds}s | Restant: ${remaining}s" -ForegroundColor Gray
            $lastUpdate = $remaining
        }
        
        Start-Sleep -Seconds 5
        
        # Vérifier si le processus est toujours actif
        if ($monitorProcess.HasExited) {
            Write-Host "`n⚠️ Le processus de monitoring s'est arrêté prématurément (code: $($monitorProcess.ExitCode))" -ForegroundColor Yellow
            break
        }
    }
    
    # Arrêter le monitoring
    if (-not $monitorProcess.HasExited) {
        Write-Host "`n🛑 Arrêt du monitoring..." -ForegroundColor Yellow
        Stop-Process -Id $monitorProcess.Id -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 2
    }
    
    Write-Host "✅ Monitoring terminé !" -ForegroundColor Green
    Write-Host ""
    
    # Vérifier le fichier de log
    if (Test-Path $logFile) {
        $fileSize = (Get-Item $logFile).Length
        $lineCount = (Get-Content $logFile -ErrorAction SilentlyContinue | Measure-Object -Line).Lines
        
        Write-Host "📊 Statistiques du log:" -ForegroundColor Cyan
        Write-Host "   - Fichier: $logFile" -ForegroundColor Gray
        Write-Host "   - Taille: $([math]::Round($fileSize / 1KB, 2)) KB" -ForegroundColor Gray
        Write-Host "   - Lignes: $lineCount" -ForegroundColor Gray
        Write-Host ""
        Write-Host "✅ Log enregistré avec succès !" -ForegroundColor Green
        Write-Host "   Vous pouvez maintenant analyser le fichier: $logFile" -ForegroundColor Cyan
    } else {
        Write-Host "⚠️ Aucun fichier de log généré" -ForegroundColor Yellow
    }
    
    # Vérifier s'il y a des erreurs
    $errorFile = "$logFile.errors"
    if (Test-Path $errorFile) {
        $errorSize = (Get-Item $errorFile).Length
        if ($errorSize -gt 0) {
            Write-Host "⚠️ Fichier d'erreurs généré: $errorFile ($([math]::Round($errorSize / 1KB, 2)) KB)" -ForegroundColor Yellow
        }
    }
    
} catch {
    Write-Host "❌ Erreur: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "=== TERMINÉ ===" -ForegroundColor Green
