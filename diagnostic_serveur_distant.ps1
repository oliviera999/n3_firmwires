# =============================================================================
# Script de diagnostic des échanges serveur distant
# =============================================================================
# Description:
#   Analyse les logs pour diagnostiquer les échanges avec le serveur distant
#   (POST données, GET commandes, Heartbeat) et vérifie la cohérence avec le code.
#
# Usage:
#   .\diagnostic_serveur_distant.ps1 -LogFile "monitor_wroom_test_*.log"
#
# =============================================================================

param(
    [string]$LogFile = ""
)

if ([string]::IsNullOrEmpty($LogFile)) {
    # Trouver le dernier fichier de log
    $LogFile = Get-ChildItem -Filter "monitor_wroom_test_*.log" | 
               Sort-Object LastWriteTime -Descending | 
               Select-Object -First 1 -ExpandProperty FullName
    
    if (-not $LogFile) {
        Write-Host "❌ Aucun fichier de log trouvé" -ForegroundColor Red
        exit 1
    }
}

Write-Host "=== DIAGNOSTIC ÉCHANGES SERVEUR DISTANT ===" -ForegroundColor Green
Write-Host "Fichier: $LogFile" -ForegroundColor Cyan
Write-Host ""

if (-not (Test-Path $LogFile)) {
    Write-Host "❌ Fichier non trouvé: $LogFile" -ForegroundColor Red
    exit 1
}

$lines = Get-Content $LogFile

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$outputFile = "diagnostic_serveur_distant_$timestamp.txt"

# =============================================================================
# 1. ANALYSE ENVOI POST DONNÉES CAPTEURS
# =============================================================================

Write-Host "📤 Analyse envoi POST données capteurs..." -ForegroundColor Yellow

# Patterns POST
$postStarts = $lines | Select-String -Pattern "\[PR\] === DÉBUT POSTRAW ==="
$postSuccess = $lines | Select-String -Pattern "\[PR\] Primary server result: SUCCESS|\[PR\] Final result: SUCCESS"
$postFailed = $lines | Select-String -Pattern "\[PR\] Primary server result: FAILED|\[PR\] Final result: FAILED"
$postDurations = $lines | Select-String -Pattern "\[PR\] Durée totale: (\d+) ms"
$syncQueue = $lines | Select-String -Pattern "\[Sync\] Queue|DataQueue.*push|Queue.*pleine"
$postMutexError = $lines | Select-String -Pattern "\[PR\] ⛔ Impossible d'acquérir mutex TLS"

# Calculer durées moyennes
$durations = @()
foreach ($match in $postDurations) {
    if ($match.Line -match "(\d+) ms") {
        $durations += [int]$matches[1]
    }
}
$avgDuration = if ($durations.Count -gt 0) { [math]::Round(($durations | Measure-Object -Average).Average, 2) } else { 0 }
$maxDuration = if ($durations.Count -gt 0) { ($durations | Measure-Object -Maximum).Maximum } else { 0 }
$minDuration = if ($durations.Count -gt 0) { ($durations | Measure-Object -Minimum).Minimum } else { 0 }

# Vérifier fréquence (attendu: toutes les 2 minutes = 120 secondes)
$postTimestamps = @()
foreach ($line in $lines) {
    if ($line -match "\[PR\] === DÉBUT POSTRAW ===") {
        # Essayer d'extraire un timestamp si présent
        if ($line -match "(\d{4}-\d{2}-\d{2}[\s:]\d{2}:\d{2}:\d{2})") {
            $postTimestamps += [DateTime]::Parse($matches[1])
        }
    }
}

$postIntervals = @()
if ($postTimestamps.Count -gt 1) {
    for ($i = 1; $i -lt $postTimestamps.Count; $i++) {
        $interval = ($postTimestamps[$i] - $postTimestamps[$i-1]).TotalSeconds
        $postIntervals += $interval
    }
}
$avgInterval = if ($postIntervals.Count -gt 0) { [math]::Round(($postIntervals | Measure-Object -Average).Average, 2) } else { 0 }

# Vérifier endpoint (attendu: /ffp3/post-data-test pour test)
$postEndpoints = $lines | Select-String -Pattern "post-data-test|post-data[^-]"
$endpointTest = ($postEndpoints | Where-Object { $_.Line -match "post-data-test" }).Count
$endpointProd = ($postEndpoints | Where-Object { $_.Line -match "post-data[^-]" -and $_.Line -notmatch "post-data-test" }).Count

# =============================================================================
# 2. ANALYSE RÉCEPTION GET COMMANDES SERVEUR
# =============================================================================

Write-Host "📥 Analyse réception GET commandes serveur..." -ForegroundColor Yellow

$getFetch = $lines | Select-String -Pattern "\[Sync\] Fetch remote state|Fetch remote state"
$jsonParseSuccess = $lines | Select-String -Pattern "JSON parsed successfully|Nettoye prefixe chunked"
$jsonParseErrors = $lines | Select-String -Pattern "JSON parse error|JSON.*fail|Parse.*error"
$getEndpoints = $lines | Select-String -Pattern "outputs-test/state|outputs/state"
$ackSent = $lines | Select-String -Pattern "ack_command|ACK.*envoyé"

# Vérifier fréquence GET (attendu: toutes les 12 secondes selon REMOTE_FETCH_INTERVAL_MS)
$getTimestamps = @()
foreach ($line in $lines) {
    if ($line -match "\[Sync\] Fetch remote state|Fetch remote state") {
        if ($line -match "(\d{4}-\d{2}-\d{2}[\s:]\d{2}:\d{2}:\d{2})") {
            $getTimestamps += [DateTime]::Parse($matches[1])
        }
    }
}

$getIntervals = @()
if ($getTimestamps.Count -gt 1) {
    for ($i = 1; $i -lt $getTimestamps.Count; $i++) {
        $interval = ($getTimestamps[$i] - $getTimestamps[$i-1]).TotalSeconds
        $getIntervals += $interval
    }
}
$avgGetInterval = if ($getIntervals.Count -gt 0) { [math]::Round(($getIntervals | Measure-Object -Average).Average, 2) } else { 0 }

# =============================================================================
# 3. ANALYSE HEARTBEAT
# =============================================================================

Write-Host "💓 Analyse heartbeat..." -ForegroundColor Yellow

$hbStarts = $lines | Select-String -Pattern "\[HB\] === DÉBUT HEARTBEAT ==="
$hbSuccess = $lines | Select-String -Pattern "\[HB\] Succès: OUI"
$hbFailed = $lines | Select-String -Pattern "\[HB\] Succès: NON"
$hbDurations = $lines | Select-String -Pattern "\[HB\] Durée totale: (\d+) ms"

$hbDurationsList = @()
foreach ($match in $hbDurations) {
    if ($match.Line -match "(\d+) ms") {
        $hbDurationsList += [int]$matches[1]
    }
}
$avgHbDuration = if ($hbDurationsList.Count -gt 0) { [math]::Round(($hbDurationsList | Measure-Object -Average).Average, 2) } else { 0 }

# =============================================================================
# 4. ANALYSE GESTION ERREURS RÉSEAU
# =============================================================================

Write-Host "🌐 Analyse gestion erreurs réseau..." -ForegroundColor Yellow

$wifiDisconnect = $lines | Select-String -Pattern "WiFi.*disconnect|WiFi.*deconnect|Connexion.*perdue"
$wifiConnect = $lines | Select-String -Pattern "WiFi.*connect|WiFi.*connected|Connexion.*reussie"
$tlsErrors = $lines | Select-String -Pattern "TLS.*error|SSL.*error|TLS.*fail"
$httpErrors = $lines | Select-String -Pattern "HTTP.*error|HTTP.*fail|HTTP.*timeout"
$timeouts = $lines | Select-String -Pattern "Timeout|TIMEOUT|timeout"
$queueReplay = $lines | Select-String -Pattern "replayQueuedData|Queue.*replay|Replay.*queue"

# =============================================================================
# 5. VÉRIFICATIONS DE COHÉRENCE AVEC LE CODE
# =============================================================================

Write-Host "🔍 Vérifications de cohérence avec le code..." -ForegroundColor Yellow

$coherence = @()

# POST: Fréquence attendue 120 secondes (REMOTE_SEND_INTERVAL_MS)
if ($avgInterval -gt 0) {
    if ($avgInterval -ge 100 -and $avgInterval -le 140) {
        $coherence += "✅ POST: Fréquence cohérente (~$avgInterval s, attendu: 120s)"
    } else {
        $coherence += "⚠️ POST: Fréquence anormale ($avgInterval s, attendu: 120s)"
    }
} else {
    $coherence += "❌ POST: Impossible de calculer la fréquence"
}

# GET: Fréquence attendue 12 secondes (REMOTE_FETCH_INTERVAL_MS)
if ($avgGetInterval -gt 0) {
    if ($avgGetInterval -ge 8 -and $avgGetInterval -le 20) {
        $coherence += "✅ GET: Fréquence cohérente (~$avgGetInterval s, attendu: 12s)"
    } else {
        $coherence += "⚠️ GET: Fréquence anormale ($avgGetInterval s, attendu: 12s)"
    }
} else {
    $coherence += "❌ GET: Impossible de calculer la fréquence"
}

# POST: Durée < 5 secondes (REQUEST_TIMEOUT_MS = 5000)
if ($maxDuration -gt 0) {
    if ($maxDuration -lt 5000) {
        $coherence += "✅ POST: Durées dans les limites (max: $maxDuration ms, limite: 5000 ms)"
    } else {
        $coherence += "⚠️ POST: Durées trop longues (max: $maxDuration ms, limite: 5000 ms)"
    }
}

# Endpoint test utilisé
if ($endpointTest -gt 0) {
    $coherence += "✅ Endpoint: Utilisation endpoint test (/ffp3/post-data-test)"
} elseif ($endpointProd -gt 0) {
    $coherence += "⚠️ Endpoint: Utilisation endpoint prod au lieu de test"
} else {
    $coherence += "❌ Endpoint: Aucun endpoint détecté"
}

# =============================================================================
# 6. GÉNÉRATION DU RAPPORT
# =============================================================================

$report = @"
=== DIAGNOSTIC ÉCHANGES SERVEUR DISTANT ===
Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
Fichier log: $LogFile

=== 1. ENVOI POST DONNÉES CAPTEURS ===

Statistiques:
- Débuts POST détectés: $($postStarts.Count)
- POST réussis: $($postSuccess.Count)
- POST échoués: $($postFailed.Count)
- Taux de succès: $(if ($postStarts.Count -gt 0) { [math]::Round(($postSuccess.Count / $postStarts.Count) * 100, 2) } else { 0 })%

Performance:
- Durée moyenne: $avgDuration ms
- Durée min: $minDuration ms
- Durée max: $maxDuration ms
- Intervalle moyen: $avgInterval secondes (attendu: 120s)

Endpoints:
- Utilisation endpoint test: $endpointTest
- Utilisation endpoint prod: $endpointProd

Queue persistante:
- Utilisations queue: $($syncQueue.Count)
- Erreurs mutex TLS: $($postMutexError.Count)

=== 2. RÉCEPTION GET COMMANDES SERVEUR ===

Statistiques:
- Tentatives GET: $($getFetch.Count)
- Parsing JSON réussis: $($jsonParseSuccess.Count)
- Erreurs parsing JSON: $($jsonParseErrors.Count)
- Taux de succès parsing: $(if ($getFetch.Count -gt 0) { [math]::Round(($jsonParseSuccess.Count / $getFetch.Count) * 100, 2) } else { 0 })%

Performance:
- Intervalle moyen: $avgGetInterval secondes (attendu: 12s)

Endpoints:
- Utilisation endpoint test: $(($getEndpoints | Where-Object { $_.Line -match "outputs-test" }).Count)
- Utilisation endpoint prod: $(($getEndpoints | Where-Object { $_.Line -match "outputs/state" -and $_.Line -notmatch "outputs-test" }).Count)

ACK:
- ACK envoyés: $($ackSent.Count)

=== 3. HEARTBEAT ===

Statistiques:
- Heartbeats détectés: $($hbStarts.Count)
- Heartbeats réussis: $($hbSuccess.Count)
- Heartbeats échoués: $($hbFailed.Count)
- Taux de succès: $(if ($hbStarts.Count -gt 0) { [math]::Round(($hbSuccess.Count / $hbStarts.Count) * 100, 2) } else { 0 })%

Performance:
- Durée moyenne: $avgHbDuration ms

=== 4. GESTION ERREURS RÉSEAU ===

WiFi:
- Déconnexions: $($wifiDisconnect.Count)
- Reconnexions: $($wifiConnect.Count)

Erreurs:
- Erreurs TLS: $($tlsErrors.Count)
- Erreurs HTTP: $($httpErrors.Count)
- Timeouts: $($timeouts.Count)

Queue:
- Replays queue: $($queueReplay.Count)

=== 5. VÉRIFICATIONS DE COHÉRENCE AVEC LE CODE ===

$($coherence -join "`n")

=== RÉSUMÉ ===

POST:
- $(if ($postStarts.Count -gt 0 -and ($postSuccess.Count / $postStarts.Count) -gt 0.8) { "✅ OK" } elseif ($postStarts.Count -gt 0) { "⚠️ Taux de succès faible" } else { "❌ Aucun POST détecté" })

GET:
- $(if ($getFetch.Count -gt 0 -and ($jsonParseErrors.Count / $getFetch.Count) -lt 0.1) { "✅ OK" } elseif ($getFetch.Count -gt 0) { "⚠️ Erreurs parsing fréquentes" } else { "❌ Aucun GET détecté" })

Heartbeat:
- $(if ($hbStarts.Count -gt 0 -and ($hbSuccess.Count / $hbStarts.Count) -gt 0.8) { "✅ OK" } elseif ($hbStarts.Count -gt 0) { "⚠️ Taux de succès faible" } else { "ℹ️ Aucun heartbeat détecté" })

Réseau:
- $(if ($wifiDisconnect.Count -eq 0) { "✅ WiFi stable" } else { "⚠️ Déconnexions WiFi détectées" })

=== FIN DU DIAGNOSTIC ===
"@

$report | Out-File -FilePath $outputFile -Encoding UTF8

Write-Host ""
Write-Host "✅ Diagnostic sauvegardé: $outputFile" -ForegroundColor Green
Write-Host ""
Write-Host "=== RÉSUMÉ ===" -ForegroundColor Cyan
Write-Host "POST: $($postStarts.Count) tentatives, $($postSuccess.Count) réussis, $($postFailed.Count) échoués" -ForegroundColor White
Write-Host "GET: $($getFetch.Count) tentatives, $($jsonParseSuccess.Count) parsing réussis, $($jsonParseErrors.Count) erreurs" -ForegroundColor White
Write-Host "Heartbeat: $($hbStarts.Count) tentatives, $($hbSuccess.Count) réussis" -ForegroundColor White
Write-Host ""
