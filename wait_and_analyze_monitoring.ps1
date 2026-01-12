# Script pour attendre la fin du monitoring et analyser
param(
    [string]$LogFile,
    [int]$TimeoutSeconds = 960  # 16 minutes max (15 min + 1 min de marge)
)

if (-not $LogFile) {
    Write-Host "Usage: .\wait_and_analyze_monitoring.ps1 -LogFile <nom_fichier.log>" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $LogFile)) {
    Write-Host "Fichier de log non trouvé: $LogFile" -ForegroundColor Red
    exit 1
}

Write-Host "=== ATTENTE FIN DU MONITORING ===" -ForegroundColor Green
Write-Host "Fichier: $LogFile" -ForegroundColor Cyan
Write-Host ""

$startTime = Get-Date
$processRunning = $true
$lastSize = 0
$noChangeCount = 0

while ($processRunning -and ((Get-Date) - $startTime).TotalSeconds -lt $TimeoutSeconds) {
    $file = Get-Item $LogFile -ErrorAction SilentlyContinue
    if ($file) {
        $currentSize = $file.Length
        $lastWrite = $file.LastWriteTime
        $elapsed = (Get-Date) - $startTime
        
        if ($currentSize -eq $lastSize) {
            $noChangeCount++
        } else {
            $noChangeCount = 0
        }
        
        # Si le fichier n'a pas changé depuis 30 secondes, le processus est probablement terminé
        if ($noChangeCount -gt 6 -or ((Get-Date) - $lastWrite).TotalSeconds -gt 30) {
            Write-Host "`nMonitoring terminé (fichier stable depuis 30s)" -ForegroundColor Green
            $processRunning = $false
            break
        }
        
        if ((Get-Date).Second % 30 -eq 0) {
            Write-Host "[$(Get-Date -Format 'HH:mm:ss')] Monitoring en cours... Taille: $([math]::Round($currentSize / 1KB, 2)) KB | Temps écoulé: $([math]::Floor($elapsed.TotalMinutes))m$([math]::Floor($elapsed.TotalSeconds % 60))s" -ForegroundColor Gray
        }
        
        $lastSize = $currentSize
    }
    
    Start-Sleep -Seconds 5
}

Write-Host ""
Write-Host "=== ANALYSE DU LOG ===" -ForegroundColor Cyan
Write-Host ""

if (Test-Path $LogFile) {
    $file = Get-Item $LogFile
    $lineCount = (Get-Content $LogFile -ErrorAction SilentlyContinue | Measure-Object -Line).Lines
    
    Write-Host "📊 Statistiques:" -ForegroundColor Yellow
    Write-Host "  - Fichier: $($file.Name)" -ForegroundColor Gray
    Write-Host "  - Taille: $([math]::Round($file.Length / 1KB, 2)) KB" -ForegroundColor Gray
    Write-Host "  - Lignes: $lineCount" -ForegroundColor Gray
    Write-Host ""
    
    # Recherche des logs de stack
    Write-Host "📈 Analyse de la stack:" -ForegroundColor Yellow
    $stackLogs = Select-String -Path $LogFile -Pattern "\[autoTask\].*Stack.*utilisée:" | Select-Object -Last 20
    if ($stackLogs) {
        $stackValues = @()
        foreach ($line in $stackLogs) {
            if ($line.Line -match "(\d+\.\d+)%") {
                $percent = [float]$matches[1]
                $stackValues += $percent
            }
        }
        if ($stackValues.Count -gt 0) {
            $minStack = [math]::Round(($stackValues | Measure-Object -Minimum).Minimum, 1)
            $maxStack = [math]::Round(($stackValues | Measure-Object -Maximum).Maximum, 1)
            $avgStack = [math]::Round(($stackValues | Measure-Object -Average).Average, 1)
            Write-Host "  - Utilisation min: $minStack%" -ForegroundColor Gray
            Write-Host "  - Utilisation max: $maxStack%" -ForegroundColor Gray
            Write-Host "  - Utilisation moyenne: $avgStack%" -ForegroundColor Gray
        }
    }
    Write-Host ""
    
    # Recherche de crashes
    Write-Host "🔍 Recherche d'erreurs critiques:" -ForegroundColor Yellow
    $critical = Select-String -Path $LogFile -Pattern "Guru Meditation|Panic|Stack canary|Rebooting" -CaseSensitive:$false
    if ($critical) {
        Write-Host "  ❌ Erreurs critiques trouvées: $($critical.Count)" -ForegroundColor Red
        $critical | Select-Object -First 5 | ForEach-Object {
            Write-Host "    - $($_.Line.Trim())" -ForegroundColor Red
        }
    } else {
        Write-Host "  ✅ Aucune erreur critique détectée" -ForegroundColor Green
    }
    Write-Host ""
    
    Write-Host "✅ Analyse terminée!" -ForegroundColor Green
    Write-Host "   Fichier complet: $($file.FullName)" -ForegroundColor Cyan
} else {
    Write-Host "❌ Fichier de log non trouvé" -ForegroundColor Red
}
