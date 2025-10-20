# Test Phase 3 simple
Write-Host "=== TEST PHASE 3 SIMPLE ===" -ForegroundColor Cyan

$port = "COM3"
$baudRate = 115200
$duration = 90
$logFile = "test_phase3_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"

Write-Host "Port: $port" -ForegroundColor Green
Write-Host "Duree: $duration secondes" -ForegroundColor Green
Write-Host "Log: $logFile" -ForegroundColor Green
Write-Host ""

# Verifier le port
if (-not (Get-WmiObject -Class Win32_SerialPort | Where-Object { $_.DeviceID -eq $port })) {
    Write-Host "Port $port non trouve" -ForegroundColor Red
    exit 1
}

Write-Host "Port $port trouve" -ForegroundColor Green
Write-Host ""

# Capturer les logs
Write-Host "Demarrage capture logs..." -ForegroundColor Green

try {
    $serial = New-Object System.IO.Ports.SerialPort $port, $baudRate, None, 8, One
    $serial.Open()
    
    $startTime = Get-Date
    $logContent = @()
    
    while ((Get-Date) - $startTime -lt [TimeSpan]::FromSeconds($duration)) {
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
    Write-Host "Logs sauvegardes dans: $logFile" -ForegroundColor Green
    
    # Analyser les logs
    Write-Host ""
    Write-Host "=== ANALYSE DES LOGS ===" -ForegroundColor Cyan
    
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
        $matches = Select-String -Path $logFile -Pattern $feature -AllMatches
        if ($matches) {
            $foundFeatures += $feature
            Write-Host "Trouve: $feature" -ForegroundColor Green
        } else {
            $missingFeatures += $feature
            Write-Host "Manquant: $feature" -ForegroundColor Red
        }
    }
    
    Write-Host ""
    Write-Host "=== RESULTATS ===" -ForegroundColor Cyan
    Write-Host "Fonctionnalites trouvees: $($foundFeatures.Count)/$($phase3Features.Count)" -ForegroundColor Green
    Write-Host "Fonctionnalites manquantes: $($missingFeatures.Count)" -ForegroundColor Red
    
    if ($missingFeatures.Count -eq 0) {
        Write-Host ""
        Write-Host "TOUS LES TESTS PHASE 3 REUSSIS !" -ForegroundColor Green
        Write-Host "Les optimisations NVS Phase 3 fonctionnent correctement." -ForegroundColor Green
    } else {
        Write-Host ""
        Write-Host "CERTAINS TESTS ONT ECHOUE" -ForegroundColor Yellow
        Write-Host "Fonctionnalites manquantes:" -ForegroundColor Red
        foreach ($missing in $missingFeatures) {
            Write-Host "  - $missing" -ForegroundColor Red
        }
        Write-Host ""
        Write-Host "Verifiez les logs pour plus de details" -ForegroundColor Yellow
    }
    
    # Afficher les dernieres lignes du log
    Write-Host ""
    Write-Host "=== DERNIERES LIGNES DU LOG ===" -ForegroundColor Cyan
    Get-Content $logFile | Select-Object -Last 10 | ForEach-Object { Write-Host $_ }
    
} catch {
    Write-Host "Erreur capture logs: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== FIN DU TEST ===" -ForegroundColor Cyan
