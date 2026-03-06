# Script pour vérifier l'état du monitoring en cours
param()

Write-Host "=== ÉTAT DU MONITORING ===" -ForegroundColor Cyan

# Vérifier si le processus Python est en cours
$pythonProcess = Get-Process python -ErrorAction SilentlyContinue
if ($pythonProcess) {
    Write-Host "✅ Processus Python en cours (PID: $($pythonProcess.Id))" -ForegroundColor Green
    Write-Host "   Démarrage: $($pythonProcess.StartTime)" -ForegroundColor Yellow
} else {
    Write-Host "❌ Aucun processus Python de monitoring trouvé" -ForegroundColor Red
}

Write-Host ""

# Vérifier le fichier de log
$logFile = Get-ChildItem monitor_until_crash*.log -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1

if ($logFile) {
    Write-Host "📄 Fichier de log: $($logFile.Name)" -ForegroundColor Green
    Write-Host "   Taille: $([math]::Round($logFile.Length/1KB, 2)) KB" -ForegroundColor Yellow
    Write-Host "   Dernière modification: $($logFile.LastWriteTime)" -ForegroundColor Yellow
    Write-Host ""
    
    # Vérifier les dernières lignes
    Write-Host "=== DERNIÈRES LIGNES DU LOG ===" -ForegroundColor Cyan
    Get-Content $logFile.FullName -Tail 20
    
    Write-Host ""
    Write-Host "=== RECHERCHE DE CRASH ===" -ForegroundColor Cyan
    $logContent = Get-Content $logFile.FullName -Raw
    $crashPatterns = @("Guru Meditation", "PANIC", "Brownout", "Core dump", "assert failed", "Stack overflow", "Fatal exception")
    
    $crashFound = $false
    foreach ($pattern in $crashPatterns) {
        if ($logContent -match $pattern) {
            Write-Host "🔴 CRASH DÉTECTÉ: $pattern" -ForegroundColor Red
            $crashFound = $true
        }
    }
    
    if (-not $crashFound) {
        Write-Host "✅ Aucun crash détecté pour le moment" -ForegroundColor Green
    }
} else {
    Write-Host "⚠️  Aucun fichier de log trouvé" -ForegroundColor Yellow
    Write-Host "   Le monitoring vient peut-être de démarrer..." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "=== FIN DE LA VÉRIFICATION ===" -ForegroundColor Cyan

