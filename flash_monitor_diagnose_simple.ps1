# =============================================================================
# Script simplifié: Flash, NVS, Monitoring 5min et Diagnostic
# =============================================================================

param(
    [string]$ComPort = "COM4",
    [int]$MonitorDuration = 300  # 5 minutes
)

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$logFile = "monitor_wroom_test_$timestamp.log"
$reportFile = "rapport_diagnostic_$timestamp.md"

Write-Host "=== FLASH, MONITORING ET DIAGNOSTIC SIMPLIFIÉ ===" -ForegroundColor Green
Write-Host "Port: $ComPort" -ForegroundColor Cyan
Write-Host "Durée monitoring: $MonitorDuration secondes (5 minutes)" -ForegroundColor Cyan
Write-Host ""

# =============================================================================
# 1. FLASH FIRMWARE
# =============================================================================
Write-Host "=== 1. FLASH FIRMWARE ===" -ForegroundColor Yellow

# Arrêter processus bloquants
Write-Host "Arrêt des processus bloquants..." -ForegroundColor Gray
Get-Process | Where-Object { 
    $_.ProcessName -like "*python*" -or 
    $_.ProcessName -like "*pio*" -or
    $_.MainWindowTitle -like "*monitor*" -or
    $_.MainWindowTitle -like "*serial*"
} | ForEach-Object {
    try {
        Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
        Write-Host "  - Arrêté: $($_.ProcessName)" -ForegroundColor Gray
    } catch { }
}
Start-Sleep -Seconds 5

# Vérifier que le port est disponible
Write-Host "Vérification du port $ComPort..." -ForegroundColor Gray
$portAvailable = $true
try {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames()
    if ($ports -notcontains $ComPort) {
        Write-Host "⚠️ Port $ComPort non trouvé. Ports disponibles: $($ports -join ', ')" -ForegroundColor Yellow
        if ($ports.Count -gt 0) {
            $ComPort = $ports[0]
            Write-Host "Utilisation de $ComPort" -ForegroundColor Yellow
        } else {
            Write-Host "❌ Aucun port série disponible" -ForegroundColor Red
            exit 1
        }
    }
} catch {
    Write-Host "⚠️ Impossible de vérifier les ports" -ForegroundColor Yellow
}

# Compiler et flasher
Write-Host "Compilation..." -ForegroundColor Gray
pio run -e wroom-test | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Erreur compilation" -ForegroundColor Red
    exit 1
}

Write-Host "Flash firmware..." -ForegroundColor Gray
pio run -e wroom-test --target upload | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Erreur flash firmware" -ForegroundColor Red
    exit 1
}

Write-Host "Flash filesystem..." -ForegroundColor Gray
Start-Sleep -Seconds 2
pio run -e wroom-test --target uploadfs | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Erreur flash filesystem" -ForegroundColor Red
    exit 1
}

Write-Host "✅ Flash réussi" -ForegroundColor Green

# =============================================================================
# 2. RÉINITIALISATION NVS
# =============================================================================
Write-Host ""
Write-Host "=== 2. RÉINITIALISATION NVS ===" -ForegroundColor Yellow

$esptool = Get-ChildItem -Path "$env:USERPROFILE\.platformio\packages" -Recurse -Filter "esptool.py" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($esptool) {
    Write-Host "Effacement NVS (0x9000, 0x6000)..." -ForegroundColor Gray
    & python "$($esptool.FullName)" --chip esp32 --port $ComPort erase_region 0x9000 0x6000 2>&1 | Out-Null
    Write-Host "✅ NVS réinitialisée" -ForegroundColor Green
} else {
    Write-Host "⚠️ esptool.py non trouvé, NVS sera réinitialisée au boot" -ForegroundColor Yellow
}

Start-Sleep -Seconds 5

# =============================================================================
# 3. MONITORING 5 MINUTES
# =============================================================================
Write-Host ""
Write-Host "=== 3. MONITORING 5 MINUTES ===" -ForegroundColor Yellow
Write-Host "Capture des logs..." -ForegroundColor Gray

$monitorJob = Start-Job -ScriptBlock {
    param($port, $duration, $logPath)
    $endTime = (Get-Date).AddSeconds($duration)
    $logContent = @()
    
    try {
        $process = Start-Process -FilePath "pio" -ArgumentList @("run", "--target", "monitor", "--environment", "wroom-test", "--monitor-port", $port) -NoNewWindow -PassThru -RedirectStandardOutput $logPath -RedirectStandardError "$logPath.errors"
        
        while ((Get-Date) -lt $endTime) {
            Start-Sleep -Seconds 1
            if ($process.HasExited) { break }
        }
        
        if (-not $process.HasExited) {
            $process.Kill()
        }
    } catch {
        Write-Error $_.Exception.Message
    }
} -ArgumentList $ComPort, $MonitorDuration, $logFile

# Afficher progression
$startTime = Get-Date
while ($monitorJob.State -eq "Running") {
    $elapsed = ((Get-Date) - $startTime).TotalSeconds
    $remaining = $MonitorDuration - $elapsed
    if ($remaining -gt 0) {
        $progress = [math]::Round(($elapsed / $MonitorDuration) * 100)
        Write-Progress -Activity "Monitoring" -Status "$([math]::Round($elapsed))s / $MonitorDuration s" -PercentComplete $progress
        Start-Sleep -Seconds 5
    } else {
        break
    }
}

Write-Progress -Activity "Monitoring" -Completed
$monitorJob | Wait-Job | Out-Null
$monitorJob | Remove-Job

Write-Host "✅ Monitoring terminé" -ForegroundColor Green

# =============================================================================
# 4. DIAGNOSTIC RAPIDE
# =============================================================================
Write-Host ""
Write-Host "=== 4. DIAGNOSTIC ===" -ForegroundColor Yellow

if (-not (Test-Path $logFile)) {
    Write-Host "❌ Fichier log non trouvé" -ForegroundColor Red
    exit 1
}

$lines = Get-Content $logFile
$content = $lines -join "`n"

# Statistiques de base
$postSuccess = ($lines | Select-String -Pattern "\[PR\].*SUCCESS").Count
$postFailed = ($lines | Select-String -Pattern "\[PR\].*FAILED").Count
$getSuccess = ($lines | Select-String -Pattern "JSON parsed successfully").Count
$getErrors = ($lines | Select-String -Pattern "JSON parse error").Count
$mailSent = ($lines | Select-String -Pattern "Mail envoyé avec succès").Count
$mailFailed = ($lines | Select-String -Pattern "Échec envoi mail").Count
$wifiConnect = ($lines | Select-String -Pattern "WiFi.*connect").Count
$wifiDisconnect = ($lines | Select-String -Pattern "WiFi.*disconnect").Count
$crashes = ($lines | Select-String -Pattern "Guru Meditation|panic|abort").Count
$errors = ($lines | Select-String -Pattern "ERROR|ERREUR").Count
$warnings = ($lines | Select-String -Pattern "WARN|WARNING").Count

# Vérifications cohérence
$postStarts = ($lines | Select-String -Pattern "\[PR\] === DÉBUT POSTRAW ===").Count
$expectedPosts = [math]::Floor($MonitorDuration / 120)  # Toutes les 2 minutes
$postOk = if ($postStarts -ge ($expectedPosts - 1)) { "✅" } else { "⚠️" }

$getFetches = ($lines | Select-String -Pattern "Fetch remote state").Count
$expectedGets = [math]::Floor($MonitorDuration / 12)  # Toutes les 12 secondes
$getOk = if ($getFetches -ge ($expectedGets - 5)) { "✅" } else { "⚠️" }

# =============================================================================
# 5. RAPPORT SIMPLE
# =============================================================================
Write-Host "Génération du rapport..." -ForegroundColor Gray

$report = @"
# Rapport de Diagnostic - WROOM-TEST

**Date:** $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  
**Durée monitoring:** $MonitorDuration secondes (5 minutes)  
**Fichier log:** \`$logFile\`

---

## Résumé Exécutif

**Statut:** $(if ($crashes -eq 0 -and $errors -lt 10) { "✅ STABLE" } elseif ($crashes -gt 0) { "❌ INSTABLE" } else { "⚠️ À SURVEILLER" })

---

## Statistiques

### Communication Serveur

- **POST:** $postStarts tentatives, $postSuccess réussis, $postFailed échoués $postOk
- **GET:** $getFetches tentatives, $getSuccess parsing réussis, $getErrors erreurs $getOk

### Mail

- **Mails envoyés:** $mailSent
- **Mails échoués:** $mailFailed

### Réseau

- **Connexions WiFi:** $wifiConnect
- **Déconnexions WiFi:** $wifiDisconnect

### Système

- **Crashes:** $crashes
- **Erreurs:** $errors
- **Warnings:** $warnings
- **Lignes loggées:** $($lines.Count)

---

## Vérifications de Cohérence

- **POST:** $postStarts détectés (attendu: ~$expectedPosts toutes les 2 min) $postOk
- **GET:** $getFetches détectés (attendu: ~$expectedGets toutes les 12s) $getOk
- **WiFi:** $(if ($wifiDisconnect -eq 0) { "✅ Stable" } else { "⚠️ $wifiDisconnect déconnexion(s)" })
- **Crashes:** $(if ($crashes -eq 0) { "✅ Aucun" } else { "❌ $crashes crash(es)" })

---

## Recommandations

$(if ($crashes -gt 0) {
"- ❌ **ACTION REQUISE:** Analyser les crashes dans le log
- Vérifier la configuration matérielle"
} elseif ($postStarts -lt ($expectedPosts - 1)) {
"- ⚠️ Vérifier pourquoi les POST ne sont pas envoyés régulièrement
- Vérifier la configuration réseau"
} elseif ($errors -gt 20) {
"- ⚠️ Analyser les erreurs dans le log
- Vérifier la stabilité WiFi"
} else {
"- ✅ Système stable
- Continuer le monitoring en production"
})

---

*Rapport généré automatiquement*
"@

$report | Out-File -FilePath $reportFile -Encoding UTF8

Write-Host ""
Write-Host "=== RÉSUMÉ ===" -ForegroundColor Cyan
Write-Host "POST: $postStarts tentatives, $postSuccess réussis, $postFailed échoués" -ForegroundColor White
Write-Host "GET: $getFetches tentatives, $getSuccess réussis, $getErrors erreurs" -ForegroundColor White
Write-Host "Mail: $mailSent envoyés, $mailFailed échoués" -ForegroundColor White
Write-Host "Crashes: $crashes" -ForegroundColor $(if ($crashes -eq 0) { "Green" } else { "Red" })
Write-Host ""
Write-Host "✅ Rapport sauvegardé: $reportFile" -ForegroundColor Green
Write-Host "✅ Log sauvegardé: $logFile" -ForegroundColor Green
Write-Host ""
