# Script simple pour capturer les logs ESP32
param(
    [string]$Port = "COM5",
    [int]$Duration = 180
)

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$logFile = "monitor_log_$timestamp.txt"

Write-Host "Debut du monitoring ESP32 sur $Port pendant $Duration secondes..."
Write-Host "Logs sauvegardes dans: $logFile"

try {
    $serial = New-Object System.IO.Ports.SerialPort $Port, 115200
    $serial.Open()
    
    $startTime = Get-Date
    $logContent = @()
    
    Write-Host "Connexion etablie, debut de la capture..."
    
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
    
    $logContent | Out-File -FilePath $logFile -Encoding UTF8
    
    Write-Host "Monitoring termine apres $Duration secondes"
    Write-Host "Total logs captures: $($logContent.Count)"
    
    $errors = $logContent | Where-Object { $_ -match "error|failed|exception|crash" }
    $warnings = $logContent | Where-Object { $_ -match "warning|warn" }
    $wifi = $logContent | Where-Object { $_ -match "wifi|connected|disconnected|ip" }
    $sensors = $logContent | Where-Object { $_ -match "sensor|temp|humidity|water|air" }
    
    Write-Host ""
    Write-Host "Analyse rapide:"
    Write-Host "  - Erreurs: $($errors.Count)"
    Write-Host "  - Warnings: $($warnings.Count)"
    Write-Host "  - Evenements WiFi: $($wifi.Count)"
    Write-Host "  - Lectures capteurs: $($sensors.Count)"
    
} catch {
    Write-Host "Erreur: $($_.Exception.Message)"
} finally {
    if ($serial -and $serial.IsOpen) {
        $serial.Close()
        Write-Host "Connexion fermee"
    }
}

Write-Host ""
Write-Host "Fichier de log: $logFile"
