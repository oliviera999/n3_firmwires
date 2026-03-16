# Script de vérification périodique du monitoring
# Vérifie l'état du monitoring et affiche les informations

param(
    [int]$CheckInterval = 30,  # Vérifier toutes les 30 secondes
    [int]$MaxChecks = 120      # Maximum 120 vérifications (1 heure)
)

$checkCount = 0
$lastLogSize = 0
$lastAnalysisSize = 0

Write-Host "=== VÉRIFICATION PÉRIODIQUE DU MONITORING ===" -ForegroundColor Green
Write-Host "Intervalle: $CheckInterval secondes" -ForegroundColor Yellow
Write-Host "Maximum: $MaxChecks vérifications" -ForegroundColor Yellow
Write-Host "Début: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan
Write-Host ""

while ($checkCount -lt $MaxChecks) {
    $checkCount++
    $currentTime = Get-Date -Format 'HH:mm:ss'
    
    Write-Host "[$currentTime] Vérification #$checkCount..." -ForegroundColor Gray
    
    # Chercher les fichiers de monitoring les plus récents (dossier dédié logs/ puis racine)
    $logsDir = Join-Path $PSScriptRoot "logs"
    $logFiles = @()
    $analysisFiles = @()
    if (Test-Path $logsDir) {
        $logFiles = @(Get-ChildItem -Path $logsDir -Filter "monitor_until_crash_*.log" -ErrorAction SilentlyContinue)
        $analysisFiles = @(Get-ChildItem -Path $logsDir -Filter "monitor_until_crash_analysis_*.txt" -ErrorAction SilentlyContinue)
    }
    if ($logFiles.Count -eq 0) {
        $logFiles = @(Get-ChildItem -Path $PSScriptRoot -Filter "monitor_until_crash_*.log" -ErrorAction SilentlyContinue)
    }
    if ($analysisFiles.Count -eq 0) {
        $analysisFiles = @(Get-ChildItem -Path $PSScriptRoot -Filter "monitor_until_crash_analysis_*.txt" -ErrorAction SilentlyContinue)
    }
    $logFiles = $logFiles | Sort-Object LastWriteTime -Descending
    $analysisFiles = $analysisFiles | Sort-Object LastWriteTime -Descending
    
    $latestLog = $logFiles | Select-Object -First 1
    $latestAnalysis = $analysisFiles | Select-Object -First 1
    
    # Vérifier si le processus Python est toujours actif
    $pythonProcesses = Get-Process python -ErrorAction SilentlyContinue
    $monitoringActive = $false
    if ($pythonProcesses) {
        foreach ($proc in $pythonProcesses) {
            try {
                $cmdLine = (Get-CimInstance Win32_Process -Filter "ProcessId = $($proc.Id)").CommandLine
                if ($cmdLine -like "*monitor_until_crash*") {
                    $monitoringActive = $true
                    Write-Host "  ✓ Processus de monitoring actif (PID: $($proc.Id))" -ForegroundColor Green
                    break
                }
            } catch {
                # Ignorer les erreurs
            }
        }
    }
    
    if (-not $monitoringActive) {
        Write-Host "  ⚠️  Aucun processus de monitoring détecté" -ForegroundColor Yellow
    }
    
    # Analyser les fichiers de log
    if ($latestLog) {
        $logSize = $latestLog.Length
        $logModified = $latestLog.LastWriteTime
        $logAge = (Get-Date) - $logModified
        
        Write-Host "  📄 Fichier de log: $($latestLog.Name)" -ForegroundColor Cyan
        Write-Host "     Taille: $([math]::Round($logSize/1024, 1)) KB" -ForegroundColor White
        Write-Host "     Modifié: $($logModified.ToString('HH:mm:ss')) (il y a $([math]::Round($logAge.TotalSeconds, 0))s)" -ForegroundColor White
        
        # Vérifier si le fichier a grandi (monitoring actif)
        if ($logSize -gt $lastLogSize) {
            Write-Host "     ✓ Fichier en croissance (monitoring actif)" -ForegroundColor Green
            $lastLogSize = $logSize
            
            # Lire les dernières lignes pour détecter un crash
            try {
                $lastLines = Get-Content $latestLog.FullName -Tail 20 -ErrorAction SilentlyContinue
                $crashDetected = $false
                $resetReason = $null
                
                foreach ($line in $lastLines) {
                    if ($line -match "Reset reason|reset reason|rst:0x|BOOT FFP5CS|REBOOT DÉTECTÉ") {
                        $crashDetected = $true
                        if ($line -match "Reset reason:\s*(\d+)") {
                            $resetReason = $matches[1]
                        }
                        Write-Host "     🔄 Redémarrage détecté dans les logs!" -ForegroundColor Red
                        Write-Host "        Ligne: $($line.Trim())" -ForegroundColor Yellow
                    }
                    if ($line -match "Guru Meditation|Panic|PANIC|Brownout") {
                        Write-Host "     ⚠️  Erreur critique détectée!" -ForegroundColor Red
                        Write-Host "        Ligne: $($line.Trim())" -ForegroundColor Yellow
                    }
                }
            } catch {
                # Ignorer les erreurs de lecture
            }
        } else {
            Write-Host "     ⏸️  Fichier stable (monitoring peut-être terminé)" -ForegroundColor Yellow
        }
    } else {
        Write-Host "  ⚠️  Aucun fichier de log trouvé" -ForegroundColor Yellow
    }
    
    # Vérifier les fichiers d'analyse
    if ($latestAnalysis) {
        $analysisSize = $latestAnalysis.Length
        $analysisModified = $latestAnalysis.LastWriteTime
        $analysisAge = (Get-Date) - $analysisModified
        
        if ($analysisSize -gt $lastAnalysisSize) {
            Write-Host "  📊 Fichier d'analyse: $($latestAnalysis.Name)" -ForegroundColor Cyan
            Write-Host "     Taille: $([math]::Round($analysisSize/1024, 1)) KB" -ForegroundColor White
            Write-Host "     Modifié: $($analysisModified.ToString('HH:mm:ss'))" -ForegroundColor White
            Write-Host "     ✓ Nouvelle analyse disponible!" -ForegroundColor Green
            
            # Afficher un résumé de l'analyse
            try {
                $analysisContent = Get-Content $latestAnalysis.FullName -Raw -ErrorAction SilentlyContinue
                if ($analysisContent) {
                    if ($analysisContent -match "Problemes critiques detectes:\s*(\d+)") {
                        $criticalIssues = $matches[1]
                        Write-Host "     🔴 Problèmes critiques: $criticalIssues" -ForegroundColor $(if([int]$criticalIssues -gt 0){"Red"}else{"Green"})
                    }
                    if ($analysisContent -match "Avertissements:\s*(\d+)") {
                        $warnings = $matches[1]
                        Write-Host "     🟡 Avertissements: $warnings" -ForegroundColor $(if([int]$warnings -gt 0){"Yellow"}else{"Green"})
                    }
                    if ($analysisContent -match "Temps jusqu'au reboot:\s*([\d.]+)\s*secondes") {
                        $timeToReboot = $matches[1]
                        Write-Host "     ⏱️  Temps jusqu'au reboot: $timeToReboot secondes" -ForegroundColor Cyan
                    }
                }
            } catch {
                # Ignorer les erreurs
            }
            
            $lastAnalysisSize = $analysisSize
        }
    }
    
    Write-Host ""
    
    # Attendre avant la prochaine vérification
    if ($checkCount -lt $MaxChecks) {
        Start-Sleep -Seconds $CheckInterval
    }
}

Write-Host ""
Write-Host "=== VÉRIFICATION TERMINÉE ===" -ForegroundColor Green
Write-Host "Fin: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan
Write-Host "Total vérifications: $checkCount" -ForegroundColor Cyan
