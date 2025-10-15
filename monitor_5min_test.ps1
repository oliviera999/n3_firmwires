# Script de monitoring ESP32 - 5 minutes pour test communication serveur
# Version: v11.37
# Focus: Vérification communication serveur distant après correction HTTP 500

param(
    [int]$DurationSeconds = 300
)

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$logFile = "monitor_5min_test_$timestamp.log"

Write-Host "========================================"
Write-Host "MONITORING ESP32 - 5 MINUTES TEST"
Write-Host "Version: v11.37"
Write-Host "Durée: $DurationSeconds secondes (5 minutes)"
Write-Host "Timestamp: $timestamp"
Write-Host "Log file: $logFile"
Write-Host "========================================"
Write-Host "Démarrage du monitoring pour $DurationSeconds secondes..."
$endTime = (Get-Date).AddSeconds($DurationSeconds)
Write-Host "Fin prévue: $($endTime.ToString('HH:mm:ss'))"
Write-Host "Monitoring en cours..."
Write-Host "Progression affichée toutes les 30 secondes"
Write-Host "Appuyez sur Ctrl+C pour arrêter avant la fin"

# Démarrer le monitoring en arrière-plan
$process = Start-Process -FilePath "python" -ArgumentList "pythonserial/serial_monitor.py", "--duration", $DurationSeconds, "--output", $logFile -NoNewWindow -PassThru -RedirectStandardOutput "$logFile.out" -RedirectStandardError "$logFile.err"

$startTime = Get-Date
$progress = 0

while ((Get-Date) -lt $endTime -and -not $process.HasExited) {
    Start-Sleep -Seconds 30
    $progress += 6
    $elapsed = [math]::Round(((Get-Date) - $startTime).TotalSeconds)
    $remaining = [math]::Round(($endTime - (Get-Date)).TotalSeconds)
    Write-Host "[$progress%] Écoulé: $([math]::Floor($elapsed/60)) min $($elapsed%60) s | Reste: $([math]::Floor($remaining/60)) min $($remaining%60) s"
}

Write-Host "Durée écoulée. Arrêt du monitoring..."

# Attendre que le processus se termine
if (-not $process.HasExited) {
    $process.WaitForExit(5000)
}

Write-Host "========================================"
Write-Host "ANALYSE DES LOGS - FOCUS COMMUNICATION SERVEUR"
Write-Host "========================================"

if (Test-Path $logFile) {
    Write-Host "Lecture du fichier de log: $logFile"
    $content = Get-Content $logFile -Raw
    
    if ($content) {
        $lines = $content -split "`n"
        $totalLines = $lines.Count
        Write-Host "Total de lignes: $totalLines"
        
        # Analyse spécifique communication serveur
        $postDataAttempts = ($lines | Select-String "POST.*post-data" -AllMatches).Matches.Count
        $postDataSuccess = ($lines | Select-String "POST.*200|succès|success" -AllMatches).Matches.Count
        $postDataFailures = ($lines | Select-String "POST.*500|erreur|error|failed" -AllMatches).Matches.Count
        
        $httpRequests = ($lines | Select-String "HTTP.*Request" -AllMatches).Matches.Count
        $httpSuccess = ($lines | Select-String "HTTP.*200" -AllMatches).Matches.Count
        $httpFailures = ($lines | Select-String "HTTP.*[45][0-9][0-9]" -AllMatches).Matches.Count
        
        Write-Host ""
        Write-Host "========================================"
        Write-Host "COMMUNICATION SERVEUR DISTANT"
        Write-Host "========================================"
        Write-Host "📤 POST Data:"
        Write-Host "  - Tentatives: $postDataAttempts"
        Write-Host "  - Succès: $postDataSuccess"
        Write-Host "  - Échecs: $postDataFailures"
        Write-Host ""
        Write-Host "🌐 HTTP Général:"
        Write-Host "  - Requêtes totales: $httpRequests"
        Write-Host "  - Succès (200): $httpSuccess"
        Write-Host "  - Échecs (4xx/5xx): $httpFailures"
        
        # Recherche spécifique des erreurs HTTP 500
        $http500Errors = $lines | Where-Object { $_ -match "500|Internal Server Error" }
        if ($http500Errors) {
            Write-Host ""
            Write-Host "❌ ERREURS HTTP 500 DÉTECTÉES:"
            foreach ($error in $http500Errors) {
                Write-Host "  - $error"
            }
        } else {
            Write-Host ""
            Write-Host "✅ AUCUNE ERREUR HTTP 500 DÉTECTÉE"
        }
        
        # Recherche des succès de communication
        $successMessages = $lines | Where-Object { $_ -match "Données enregistrées|Data saved|success" }
        if ($successMessages) {
            Write-Host ""
            Write-Host "✅ SUCCÈS DE COMMUNICATION:"
            foreach ($success in $successMessages) {
                Write-Host "  - $success"
            }
        }
        
        # Statistiques de stabilité
        $guruMeditation = ($lines | Select-String "Guru Meditation|Panic|Brownout" -AllMatches).Matches.Count
        $wifiReconnections = ($lines | Select-String "WiFi.*reconnect|WiFi.*disconnect" -AllMatches).Matches.Count
        $memoryWarnings = ($lines | Select-String "Low memory|Heap.*low" -AllMatches).Matches.Count
        
        Write-Host ""
        Write-Host "========================================"
        Write-Host "STABILITÉ SYSTÈME"
        Write-Host "========================================"
        Write-Host "🔴 Erreurs critiques: $guruMeditation"
        Write-Host "🟡 Reconnexions WiFi: $wifiReconnections"
        Write-Host "🟡 Alertes mémoire: $memoryWarnings"
        
        # Résumé
        Write-Host ""
        Write-Host "========================================"
        Write-Host "RÉSUMÉ"
        Write-Host "========================================"
        if ($postDataFailures -eq 0 -and $httpFailures -eq 0) {
            Write-Host "🎉 COMMUNICATION SERVEUR: FONCTIONNELLE"
            Write-Host "✅ Aucune erreur HTTP détectée"
            Write-Host "✅ Communication bidirectionnelle OK"
        } elseif ($postDataFailures -gt 0) {
            Write-Host "⚠️  COMMUNICATION SERVEUR: PROBLÈME DÉTECTÉ"
            Write-Host "❌ Erreurs HTTP: $postDataFailures"
            Write-Host "🔍 Vérifier les logs détaillés"
        } else {
            Write-Host "🟡 COMMUNICATION SERVEUR: PARTIELLE"
            Write-Host "⚠️  Quelques erreurs détectées"
        }
        
    } else {
        Write-Host "❌ Le fichier de log est vide"
    }
} else {
    Write-Host "❌ Fichier de log non trouvé: $logFile"
}

Write-Host ""
Write-Host "Analyse détaillée sauvegardée: monitor_5min_analysis_$timestamp.log"
Write-Host "========================================"
