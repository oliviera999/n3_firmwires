# Script de vérification des logs v11.118
# Vérifie que les simplifications n'ont pas causé de régression

Write-Host "=== VÉRIFICATION LOGS V11.118 (SIMPLIFICATIONS) ===" -ForegroundColor Green
Write-Host ""

$logFile = "esp32_logs_v11.118_$(Get-Date -Format 'yyyyMMdd_HHmmss').txt"
$duration = 180 # 3 minutes

Write-Host "📋 Capture des logs pendant $duration secondes..." -ForegroundColor Cyan
Write-Host "Log sauvegardé dans: $logFile" -ForegroundColor Gray
Write-Host ""

# Capturer les logs
$job = Start-Job -ScriptBlock {
    param($port, $baud, $outputFile, $duration)
    
    $endTime = (Get-Date).AddSeconds($duration)
    $content = ""
    
    try {
        $portObj = New-Object System.IO.Ports.SerialPort($port, $baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
        $portObj.ReadTimeout = 1000
        $portObj.Open()
        
        while ((Get-Date) -lt $endTime) {
            if ($portObj.BytesToRead -gt 0) {
                $line = $portObj.ReadLine()
                $content += $line + "`n"
                Write-Output $line
            }
            Start-Sleep -Milliseconds 100
        }
        
        $portObj.Close()
    } catch {
        Write-Error "Erreur: $_"
    }
    
    Set-Content -Path $outputFile -Value $content
} -ArgumentList "COM6", 115200, $logFile, $duration

# Afficher la progression
$startTime = Get-Date
while ($job.State -eq "Running") {
    $elapsed = [math]::Round(((Get-Date) - $startTime).TotalSeconds)
    $remaining = $duration - $elapsed
    if ($remaining -gt 0) {
        Write-Host "`r⏱️  Temps écoulé: ${elapsed}s / ${duration}s (reste ${remaining}s)" -NoNewline -ForegroundColor Yellow
    }
    Start-Sleep -Seconds 1
}

Write-Host "`n"
Write-Host "✅ Capture terminée !" -ForegroundColor Green
Write-Host ""

# Attendre la fin du job
Wait-Job $job | Out-Null
$result = Receive-Job $job
Remove-Job $job

# Analyser les logs
if (Test-Path $logFile) {
    Write-Host "📊 Analyse des logs..." -ForegroundColor Cyan
    $logContent = Get-Content $logFile -Raw
    
    # Patterns critiques
    $critical = @(
        "Guru Meditation",
        "Panic",
        "Brownout",
        "Core dump",
        "Watchdog.*timeout",
        "assert failed",
        "abort\(\)"
    )
    
    $errors = 0
    foreach ($pattern in $critical) {
        $matches = [regex]::Matches($logContent, $pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if ($matches.Count -gt 0) {
            Write-Host "  ❌ CRITIQUE: $pattern ($($matches.Count) occurrence(s))" -ForegroundColor Red
            $errors++
        }
    }
    
    if ($errors -eq 0) {
        Write-Host "  ✅ Aucune erreur critique détectée" -ForegroundColor Green
    }
    
    # Patterns de succès
    $success = @(
        "Démarrage FFP5CS",
        "Init done",
        "WiFi.*connecté",
        "/api/status",
        "jsonPool",
        "sensorCache"
    )
    
    Write-Host ""
    Write-Host "📈 Points positifs:" -ForegroundColor Cyan
    foreach ($pattern in $success) {
        $matches = [regex]::Matches($logContent, $pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if ($matches.Count -gt 0) {
            Write-Host "  ✅ $pattern : $($matches.Count) occurrence(s)" -ForegroundColor Green
        }
    }
    
    # Statistiques
    $lineCount = (Get-Content $logFile).Count
    Write-Host ""
    Write-Host "📁 Log complet: $logFile ($lineCount lignes)" -ForegroundColor Gray
    
} else {
    Write-Host "⚠️  Aucun fichier de log généré" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "=== VÉRIFICATION TERMINÉE ===" -ForegroundColor Green

