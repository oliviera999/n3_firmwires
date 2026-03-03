# Script PowerShell pour capturer les logs ESP32
param(
    [string]$Port = "COM5",
    [int]$Duration = 180,
    [int]$BaudRate = 115200
)

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$logFile = "monitor_log_$timestamp.txt"

Write-Host "🔍 Début du monitoring ESP32 sur $Port pendant $Duration secondes..." -ForegroundColor Green
Write-Host "📄 Logs sauvegardés dans: $logFile" -ForegroundColor Yellow

$projectRoot = (Get-Item $PSScriptRoot).Parent.Parent.FullName
. (Join-Path $projectRoot "scripts\Release-ComPort.ps1")
Release-ComPortIfNeeded -Port $Port -Baud $BaudRate

try {
    # Créer la connexion série
    $serial = New-Object System.IO.Ports.SerialPort $Port, $BaudRate
    $serial.Open()
    
    $startTime = Get-Date
    $logContent = @()
    
    Write-Host "✅ Connexion établie, début de la capture..." -ForegroundColor Green
    
    while ((Get-Date) - $startTime -lt [TimeSpan]::FromSeconds($Duration)) {
        if ($serial.BytesToRead -gt 0) {
            $line = $serial.ReadLine()
            $timestamp = Get-Date -Format "HH:mm:ss.fff"
            $elapsed = ((Get-Date) - $startTime).TotalSeconds
            $elapsedStr = [math]::Round($elapsed, 2).ToString()
            $logEntry = "[$timestamp] (+$elapsedStr s) $line"
            
            Write-Host $logEntry
            $logContent += $logEntry
        }
        Start-Sleep -Milliseconds 10
    }
    
    # Sauvegarder les logs
    $logContent | Out-File -FilePath $logFile -Encoding UTF8
    
    Write-Host "Monitoring termine apres $Duration secondes" -ForegroundColor Green
    Write-Host "Total logs captures: $($logContent.Count)" -ForegroundColor Cyan
    
    # Analyse rapide
    $errors = $logContent | Where-Object { $_ -match "error|failed|exception|crash" }
    $warnings = $logContent | Where-Object { $_ -match "warning|warn" }
    $wifi = $logContent | Where-Object { $_ -match "wifi|connected|disconnected|ip" }
    $sensors = $logContent | Where-Object { $_ -match "sensor|temp|humidity|water|air" }
    
    Write-Host "`nAnalyse rapide:" -ForegroundColor Magenta
    Write-Host "  - Erreurs: $($errors.Count)" -ForegroundColor $(if($errors.Count -gt 0) {"Red"} else {"Green"})
    Write-Host "  - Warnings: $($warnings.Count)" -ForegroundColor $(if($warnings.Count -gt 0) {"Yellow"} else {"Green"})
    Write-Host "  - Evenements WiFi: $($wifi.Count)" -ForegroundColor Cyan
    Write-Host "  - Lectures capteurs: $($sensors.Count)" -ForegroundColor Cyan
    
} catch {
    Write-Host "Erreur: $($_.Exception.Message)" -ForegroundColor Red
} finally {
    if ($serial -and $serial.IsOpen) {
        $serial.Close()
        Write-Host "Connexion fermee" -ForegroundColor Yellow
    }
}

Write-Host "`nFichier de log: $logFile" -ForegroundColor Green
