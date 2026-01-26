# Script de diagnostic - Communication Serveur Distant
# Usage: .\diagnostic_communication_serveur.ps1 <fichier_log>

param(
    [Parameter(Mandatory=$true)]
    [string]$LogFile
)

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "DIAGNOSTIC COMMUNICATION SERVEUR" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

if (-not (Test-Path $LogFile)) {
    Write-Host "❌ Fichier log introuvable: $LogFile" -ForegroundColor Red
    exit 1
}

Write-Host "📄 Analyse du log: $LogFile" -ForegroundColor Yellow
Write-Host ""

# 1. Vérifier les messages d'envoi
Write-Host "1️⃣ Vérification ENVOI vers serveur..." -ForegroundColor Yellow
$sendMessages = Select-String -Path $LogFile -Pattern "\[SM\]|\[PR\]|SENDMEASUREMENTS|POSTRAW|\[Sync\].*envoi POST" -CaseSensitive:$false
if ($sendMessages) {
    Write-Host "   ✅ Messages d'envoi trouvés: $($sendMessages.Count)" -ForegroundColor Green
    $sendMessages | Select-Object -First 5 | ForEach-Object { Write-Host "      $($_.Line.Trim())" -ForegroundColor Gray }
} else {
    Write-Host "   ❌ AUCUN message d'envoi trouvé" -ForegroundColor Red
}
Write-Host ""

# 2. Vérifier les messages de réception
Write-Host "2️⃣ Vérification RÉCEPTION depuis serveur..." -ForegroundColor Yellow
$recvMessages = Select-String -Path $LogFile -Pattern "\[GET\]|\[GPIOParser\]|\[GPIO\].*reçu|RÉPONSE REMOTE|Remote JSON parsed" -CaseSensitive:$false
if ($recvMessages) {
    Write-Host "   ✅ Messages de réception trouvés: $($recvMessages.Count)" -ForegroundColor Green
    $recvMessages | Select-Object -First 5 | ForEach-Object { Write-Host "      $($_.Line.Trim())" -ForegroundColor Gray }
} else {
    Write-Host "   ❌ AUCUN message de réception trouvé" -ForegroundColor Red
}
Write-Host ""

# 3. Vérifier les diagnostics de synchronisation
Write-Host "3️⃣ Vérification diagnostics [Sync]..." -ForegroundColor Yellow
$syncMessages = Select-String -Path $LogFile -Pattern "\[Sync\]" -CaseSensitive:$false
if ($syncMessages) {
    Write-Host "   ✅ Messages [Sync] trouvés: $($syncMessages.Count)" -ForegroundColor Green
    $syncMessages | Select-Object -First 5 | ForEach-Object { Write-Host "      $($_.Line.Trim())" -ForegroundColor Gray }
} else {
    Write-Host "   ❌ AUCUN message [Sync] trouvé (devrait apparaître toutes les 30s)" -ForegroundColor Red
}
Write-Host ""

# 4. Vérifier la configuration réseau
Write-Host "4️⃣ Vérification configuration réseau..." -ForegroundColor Yellow
$configMessages = Select-String -Path $LogFile -Pattern "\[Config\].*Net|Net flags|net_send_en|net_recv_en|loadNetworkFlags" -CaseSensitive:$false
if ($configMessages) {
    Write-Host "   ✅ Messages de configuration trouvés: $($configMessages.Count)" -ForegroundColor Green
    $configMessages | Select-Object -First 5 | ForEach-Object { Write-Host "      $($_.Line.Trim())" -ForegroundColor Gray }
} else {
    Write-Host "   ⚠️  Aucun message de configuration réseau trouvé" -ForegroundColor Yellow
    Write-Host "      (peut être normal si le log ne commence pas au boot)" -ForegroundColor Gray
}
Write-Host ""

# 5. Vérifier le WiFi
Write-Host "5️⃣ Vérification WiFi..." -ForegroundColor Yellow
$wifiMessages = Select-String -Path $LogFile -Pattern "WiFi.*RSSI|WiFi.*connect|WiFi.*status" -CaseSensitive:$false
if ($wifiMessages) {
    Write-Host "   ✅ Messages WiFi trouvés: $($wifiMessages.Count)" -ForegroundColor Green
    $wifiMessages | Select-Object -First 3 | ForEach-Object { Write-Host "      $($_.Line.Trim())" -ForegroundColor Gray }
} else {
    Write-Host "   ⚠️  Peu de messages WiFi trouvés" -ForegroundColor Yellow
}
Write-Host ""

# 6. Vérifier les messages de désactivation
Write-Host "6️⃣ Vérification messages de désactivation..." -ForegroundColor Yellow
$disabledMessages = Select-String -Path $LogFile -Pattern "⛔|désactivé|disabled|bloqué|SKIP" -CaseSensitive:$false
if ($disabledMessages) {
    Write-Host "   ⚠️  Messages de désactivation trouvés: $($disabledMessages.Count)" -ForegroundColor Yellow
    $disabledMessages | Select-Object -First 5 | ForEach-Object { Write-Host "      $($_.Line.Trim())" -ForegroundColor Gray }
} else {
    Write-Host "   ✅ Aucun message de désactivation trouvé" -ForegroundColor Green
}
Write-Host ""

# 7. Statistiques du log
Write-Host "7️⃣ Statistiques du log..." -ForegroundColor Yellow
$totalLines = (Get-Content $LogFile | Measure-Object -Line).Lines
$firstLine = Get-Content $LogFile -First 1
$lastLine = Get-Content $LogFile -Last 1

Write-Host "   Lignes totales: $totalLines" -ForegroundColor Gray
Write-Host "   Première ligne: $($firstLine.Substring(0, [Math]::Min(80, $firstLine.Length)))" -ForegroundColor Gray
Write-Host "   Dernière ligne: $($lastLine.Substring(0, [Math]::Min(80, $lastLine.Length)))" -ForegroundColor Gray
Write-Host ""

# Résumé
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "RÉSUMÉ" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$issues = @()

if (-not $sendMessages) {
    $issues += "❌ Aucune donnée envoyée vers le serveur"
}
if (-not $recvMessages) {
    $issues += "❌ Aucune donnée reçue du serveur"
}
if (-not $syncMessages) {
    $issues += "⚠️  Aucun diagnostic [Sync] (AutomatismSync::update() non exécuté?)"
}

if ($issues.Count -eq 0) {
    Write-Host "✅ Aucun problème détecté dans le log" -ForegroundColor Green
} else {
    Write-Host "Problèmes détectés:" -ForegroundColor Red
    $issues | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    Write-Host ""
    Write-Host "📋 Consultez le rapport: RAPPORT_ANALYSE_COMMUNICATION_SERVEUR_2026-01-25.md" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "🔧 Actions recommandées:" -ForegroundColor Cyan
    Write-Host "  1. Vérifier les flags NVS: net_send_en et net_recv_en" -ForegroundColor White
    Write-Host "  2. Ajouter des logs de debug dans AutomatismSync::update()" -ForegroundColor White
    Write-Host "  3. Capturer un nouveau log depuis le boot complet" -ForegroundColor White
}

Write-Host ""
