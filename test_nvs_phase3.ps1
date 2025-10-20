# Test NVS Phase 3 - Rotation automatique des logs et nettoyage
# Version: 11.82

Write-Host "=== TEST NVS PHASE 3 - ROTATION AUTOMATIQUE DES LOGS ET NETTOYAGE ===" -ForegroundColor Cyan
Write-Host "Version: 11.82" -ForegroundColor Yellow
Write-Host ""

# Configuration
$port = "COM3"
$baudRate = 115200
$testDuration = 120  # 2 minutes pour observer le nettoyage
$logFile = "test_nvs_phase3_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"

Write-Host "Configuration:" -ForegroundColor Green
Write-Host "  Port: $port"
Write-Host "  Baud Rate: $baudRate"
Write-Host "  Duree: $testDuration secondes"
Write-Host "  Log: $logFile"
Write-Host ""

# Verifier que le port serie est disponible
if (-not (Get-WmiObject -Class Win32_SerialPort | Where-Object { $_.DeviceID -eq $port })) {
    Write-Host "❌ Port $port non trouve" -ForegroundColor Red
    Write-Host "Ports disponibles:" -ForegroundColor Yellow
    Get-WmiObject -Class Win32_SerialPort | ForEach-Object { Write-Host "  $($_.DeviceID): $($_.Description)" }
    exit 1
}

Write-Host "✅ Port $port trouve" -ForegroundColor Green
Write-Host ""

# Fonction pour capturer les logs
function Start-LogCapture {
    param($Port, $BaudRate, $LogFile, $Duration)
    
    Write-Host "🚀 Demarrage capture logs..." -ForegroundColor Green
    Write-Host "Appuyez sur Ctrl+C pour arreter" -ForegroundColor Yellow
    Write-Host ""
    
    try {
        # Utiliser PowerShell pour capturer les donnees serie
        $serial = New-Object System.IO.Ports.SerialPort $Port, $BaudRate, None, 8, One
        $serial.Open()
        
        $startTime = Get-Date
        $logContent = @()
        
        while ((Get-Date) - $startTime -lt [TimeSpan]::FromSeconds($Duration)) {
            if ($serial.BytesToRead -gt 0) {
                $data = $serial.ReadLine()
                $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
                $logLine = "[$timestamp] $data"
                Write-Host $logLine
                $logContent += $logLine
            }
            Start-Sleep -Milliseconds 100
        }
        
        $serial.Close()
        
        # Sauvegarder les logs
        $logContent | Out-File -FilePath $logFile -Encoding UTF8
        Write-Host ""
        Write-Host "✅ Logs sauvegardes dans: $logFile" -ForegroundColor Green
        
        return $logFile
    }
    catch {
        Write-Host "❌ Erreur capture logs: $($_.Exception.Message)" -ForegroundColor Red
        return $null
    }
}

# Demarrer la capture
$capturedLog = Start-LogCapture -Port $port -BaudRate $baudRate -LogFile $logFile -Duration $testDuration

if ($capturedLog) {
    Write-Host ""
    Write-Host "=== ANALYSE DES LOGS ===" -ForegroundColor Cyan
    
    # Analyser les logs pour les fonctionnalites Phase 3
    $phase3Features = @(
        "v11.82",
        "Version.*11.82",
        "Nettoyage periodique programme",
        "Demarrage nettoyage periodique NVS",
        "Rotation des logs",
        "Nettoyage donnees anciennes",
        "Nettoyage des cles obsoletes",
        "Cle.*supprimee",
        "Nettoyage periodique NVS termine"
    )
    
    $foundFeatures = @()
    $missingFeatures = @()
    
    foreach ($feature in $phase3Features) {
        $matches = Select-String -Path $capturedLog -Pattern $feature -AllMatches
        if ($matches) {
            $foundFeatures += $feature
            Write-Host "✅ Trouve: $feature" -ForegroundColor Green
        } else {
            $missingFeatures += $feature
            Write-Host "❌ Manquant: $feature" -ForegroundColor Red
        }
    }
    
    Write-Host ""
    Write-Host "=== RESULTATS ===" -ForegroundColor Cyan
    Write-Host "Fonctionnalites trouvees: $($foundFeatures.Count)/$($phase3Features.Count)" -ForegroundColor Green
    Write-Host "Fonctionnalites manquantes: $($missingFeatures.Count)" -ForegroundColor Red
    
    if ($missingFeatures.Count -eq 0) {
        Write-Host ""
        Write-Host "🎉 TOUS LES TESTS PHASE 3 REUSSIS !" -ForegroundColor Green
        Write-Host "Les optimisations NVS Phase 3 fonctionnent correctement." -ForegroundColor Green
    } else {
        Write-Host ""
        Write-Host "⚠️ CERTAINS TESTS ONT ECHOUE" -ForegroundColor Yellow
        Write-Host "Fonctionnalites manquantes:" -ForegroundColor Red
        foreach ($missing in $missingFeatures) {
            Write-Host "  - $missing" -ForegroundColor Red
        }
        Write-Host ""
        Write-Host "🔍 Verifiez les logs pour plus de details" -ForegroundColor Yellow
    }
    
    # Afficher les dernieres lignes du log
    Write-Host ""
    Write-Host "=== DERNIERES LIGNES DU LOG ===" -ForegroundColor Cyan
    Get-Content $capturedLog | Select-Object -Last 10 | ForEach-Object { Write-Host $_ }
    
} else {
    Write-Host "❌ Echec de la capture des logs" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== FIN DU TEST ===" -ForegroundColor Cyan