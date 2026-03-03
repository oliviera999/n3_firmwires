# Libération du port COM si occupé (processus moniteur python/pio).
# À dot-sourcer depuis les scripts qui utilisent le port série.
# Usage: Release-ComPortIfNeeded -Port "COM4"

function Release-ComPortIfNeeded {
    param(
        [string]$Port = "",
        [int]$Baud = 115200
    )
    if (-not $Port) { return }
    # Tenter d'ouvrir le port : si succès, il est libre
    try {
        $sp = [System.IO.Ports.SerialPort]::new($Port, $Baud)
        $sp.Open()
        $sp.Close()
        $sp.Dispose()
        Write-Host "Port $Port deja libre." -ForegroundColor Green
        return
    } catch {
        # Port occupé ou inaccessible : arrêter les processus typiques du moniteur
    }
    Write-Host "Liberation du port $Port (arret processus moniteur)..." -ForegroundColor Yellow
    $killed = 0
    try {
        $procs = Get-Process -ErrorAction SilentlyContinue | Where-Object {
            $_.ProcessName -like "*python*" -or $_.ProcessName -like "*pio*" -or
            ($_.MainWindowTitle -and ($_.MainWindowTitle -like "*monitor*" -or $_.MainWindowTitle -like "*serial*"))
        }
        foreach ($p in $procs) {
            try {
                Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
                Write-Host "  Arrete: $($p.ProcessName) (PID $($p.Id))" -ForegroundColor Gray
                $killed++
            } catch { }
        }
        if ($killed -gt 0) {
            Start-Sleep -Seconds 3
            Write-Host "Port $Port : $killed processus arrete(s)." -ForegroundColor Green
        } else {
            Write-Host "Aucun processus moniteur detecte. Verifiez que le port est libre." -ForegroundColor Yellow
        }
    } catch {
        Write-Host "Erreur liberation port: $($_.Exception.Message)" -ForegroundColor Yellow
    }
}
