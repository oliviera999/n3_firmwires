# Monitoring 90 secondes - Version 11.68 GPIO Parsing Unifié
# Usage: .\monitor_90s_v11.68.ps1

param(
    [string]$Port = "COM3",
    [int]$Duration = 90,
    [string]$LogFile = "monitor_90s_v11.68_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"
)

Write-Host "=== MONITORING 90 SECONDES - v11.68 GPIO PARSING UNIFIÉ ===" -ForegroundColor Green
Write-Host "Port: $Port" -ForegroundColor Yellow
Write-Host "Durée: $Duration secondes" -ForegroundColor Yellow
Write-Host "Fichier: $LogFile" -ForegroundColor Yellow
Write-Host ""
Write-Host "🔍 Recherche: Errors, Warnings, GPIO, NVS, Parsing" -ForegroundColor Cyan
Write-Host ""

# Vérifier que le port existe
$ports = [System.IO.Ports.SerialPort]::getPortNames()
if ($ports -notcontains $Port) {
    Write-Host "❌ Port $Port non trouvé. Ports disponibles:" -ForegroundColor Red
    $ports | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
    exit 1
}

# Fonction pour capturer les logs
function Start-SerialMonitoring {
    param([string]$Port, [int]$Duration, [string]$LogFile)
    
    $serial = New-Object System.IO.Ports.SerialPort $Port, 115200
    $serial.ReadTimeout = 1000
    
    try {
        $serial.Open()
        Write-Host "✅ Connexion série établie sur $Port" -ForegroundColor Green
        
        $startTime = Get-Date
        $endTime = $startTime.AddSeconds($Duration)
        
        # Créer le fichier de log
        New-Item -Path $LogFile -ItemType File -Force | Out-Null
        
        Write-Host "📝 Début capture logs..." -ForegroundColor Cyan
        
        while ((Get-Date) -lt $endTime) {
            try {
                $data = $serial.ReadLine()
                if ($data) {
                    # Écrire dans le fichier
                    Add-Content -Path $LogFile -Value $data
                    
                    # Afficher avec coloration
                    if ($data -match "ERROR|❌|Panic|Guru|Brownout|Core dump") {
                        Write-Host $data -ForegroundColor Red
                    }
                    elseif ($data -match "WARN|⚠️|Warning|Timeout") {
                        Write-Host $data -ForegroundColor Yellow
                    }
                    elseif ($data -match "GPIO|GPIOParser|GPIONotifier|NVS|Parsing") {
                        Write-Host $data -ForegroundColor Cyan
                    }
                    elseif ($data -match "✓|✅|Success|OK") {
                        Write-Host $data -ForegroundColor Green
                    }
                    else {
                        Write-Host $data -ForegroundColor White
                    }
                }
            }
            catch {
                # Timeout de lecture - normal
            }
        }
        
        Write-Host ""
        Write-Host "⏰ Monitoring terminé après $Duration secondes" -ForegroundColor Green
        
    }
    catch {
        Write-Host "❌ Erreur connexion série: $($_.Exception.Message)" -ForegroundColor Red
        exit 1
    }
    finally {
        if ($serial.IsOpen) {
            $serial.Close()
        }
    }
}

# Fonction d'analyse des logs
function Analyze-Logs {
    param([string]$LogFile)
    
    Write-Host ""
    Write-Host "=== ANALYSE DES LOGS ===" -ForegroundColor Green
    
    if (-not (Test-Path $LogFile)) {
        Write-Host "❌ Fichier de log non trouvé: $LogFile" -ForegroundColor Red
        return
    }
    
    $logs = Get-Content $LogFile
    
    # Statistiques générales
    $totalLines = $logs.Count
    $errorLines = ($logs | Where-Object { $_ -match "ERROR|❌|Panic|Guru|Brownout|Core dump" }).Count
    $warningLines = ($logs | Where-Object { $_ -match "WARN|⚠️|Warning|Timeout" }).Count
    $gpioLines = ($logs | Where-Object { $_ -match "GPIO|GPIOParser|GPIONotifier" }).Count
    $nvsLines = ($logs | Where-Object { $_ -match "NVS|Sauvegardé" }).Count
    
    Write-Host "📊 Statistiques générales:" -ForegroundColor Cyan
    Write-Host "  Total lignes: $totalLines" -ForegroundColor White
    Write-Host "  Erreurs: $errorLines" -ForegroundColor $(if($errorLines -eq 0) {"Green"} else {"Red"})
    Write-Host "  Warnings: $warningLines" -ForegroundColor $(if($warningLines -eq 0) {"Green"} else {"Yellow"})
    Write-Host "  GPIO: $gpioLines" -ForegroundColor Cyan
    Write-Host "  NVS: $nvsLines" -ForegroundColor Cyan
    
    # Analyse spécifique v11.68
    Write-Host ""
    Write-Host "🔍 Analyse spécifique v11.68:" -ForegroundColor Cyan
    
    # Vérifier GPIOParser
    $parserLines = $logs | Where-Object { $_ -match "GPIOParser" }
    if ($parserLines) {
        Write-Host "  ✅ GPIOParser détecté:" -ForegroundColor Green
        $parserLines | ForEach-Object { Write-Host "    $_" -ForegroundColor White }
    } else {
        Write-Host "  ⚠️ GPIOParser non détecté" -ForegroundColor Yellow
    }
    
    # Vérifier GPIONotifier
    $notifierLines = $logs | Where-Object { $_ -match "GPIONotifier" }
    if ($notifierLines) {
        Write-Host "  ✅ GPIONotifier détecté:" -ForegroundColor Green
        $notifierLines | ForEach-Object { Write-Host "    $_" -ForegroundColor White }
    } else {
        Write-Host "  ⚠️ GPIONotifier non détecté" -ForegroundColor Yellow
    }
    
    # Vérifier sauvegarde NVS
    $nvsSaveLines = $logs | Where-Object { $_ -match "NVS.*Sauvegardé" }
    if ($nvsSaveLines) {
        Write-Host "  ✅ Sauvegarde NVS détectée:" -ForegroundColor Green
        $nvsSaveLines | ForEach-Object { Write-Host "    $_" -ForegroundColor White }
    } else {
        Write-Host "  ⚠️ Sauvegarde NVS non détectée" -ForegroundColor Yellow
    }
    
    # Vérifier parsing JSON
    $parsingLines = $logs | Where-Object { $_ -match "PARSING JSON SERVEUR|FIN PARSING" }
    if ($parsingLines) {
        Write-Host "  ✅ Parsing JSON détecté:" -ForegroundColor Green
        $parsingLines | ForEach-Object { Write-Host "    $_" -ForegroundColor White }
    } else {
        Write-Host "  ⚠️ Parsing JSON non détecté" -ForegroundColor Yellow
    }
    
    # Erreurs critiques
    if ($errorLines -gt 0) {
        Write-Host ""
        Write-Host "🚨 ERREURS CRITIQUES DÉTECTÉES:" -ForegroundColor Red
        $logs | Where-Object { $_ -match "ERROR|❌|Panic|Guru|Brownout|Core dump" } | ForEach-Object {
            Write-Host "  $_" -ForegroundColor Red
        }
    }
    
    # Warnings importants
    if ($warningLines -gt 0) {
        Write-Host ""
        Write-Host "⚠️ WARNINGS DÉTECTÉS:" -ForegroundColor Yellow
        $logs | Where-Object { $_ -match "WARN|⚠️|Warning|Timeout" } | ForEach-Object {
            Write-Host "  $_" -ForegroundColor Yellow
        }
    }
    
    # Recommandations
    Write-Host ""
    Write-Host "💡 Recommandations:" -ForegroundColor Cyan
    
    if ($errorLines -eq 0 -and $warningLines -eq 0) {
        Write-Host "  ✅ Système stable - aucun problème détecté" -ForegroundColor Green
    }
    
    if ($gpioLines -eq 0) {
        Write-Host "  ⚠️ Aucune activité GPIO détectée - vérifier connexion serveur" -ForegroundColor Yellow
    }
    
    if ($nvsLines -eq 0) {
        Write-Host "  ⚠️ Aucune sauvegarde NVS détectée - vérifier parsing GPIO" -ForegroundColor Yellow
    }
    
    if ($parserLines.Count -eq 0) {
        Write-Host "  ⚠️ GPIOParser non utilisé - vérifier intégration automatism.cpp" -ForegroundColor Yellow
    }
}

# Exécution principale
try {
    Start-SerialMonitoring -Port $Port -Duration $Duration -LogFile $LogFile
    Analyze-Logs -LogFile $LogFile
    
    Write-Host ""
    Write-Host "📁 Logs sauvegardés dans: $LogFile" -ForegroundColor Green
    Write-Host "🔍 Analyse terminée" -ForegroundColor Green
    
} catch {
    Write-Host "❌ Erreur durant le monitoring: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
