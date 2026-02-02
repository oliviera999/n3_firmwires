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
    # Trouver le dernier fichier de log (monitor_5min ou monitor_wroom_test)
    $candidates = @(Get-ChildItem -Filter "monitor_5min_*.log" -ErrorAction SilentlyContinue)
    $candidates += @(Get-ChildItem -Filter "monitor_wroom_test_*.log" -ErrorAction SilentlyContinue)
    $latest = $candidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $LogFile = if ($latest) { $latest.FullName } else { $null }

    if (-not $LogFile) {
        Write-Host '[KO] Aucun fichier de log trouve' -ForegroundColor Red
        exit 1
    }
}

Write-Host '=== DIAGNOSTIC ECHANGES SERVEUR DISTANT ===' -ForegroundColor Green
Write-Host ('Fichier: ' + $LogFile) -ForegroundColor Cyan
Write-Host ""

if (-not (Test-Path $LogFile)) {
    Write-Host ('[KO] Fichier non trouve: ' + $LogFile) -ForegroundColor Red
    exit 1
}

$lines = Get-Content $LogFile

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$outputFile = "diagnostic_serveur_distant_$timestamp.txt"

# =============================================================================
# 1. ANALYSE ENVOI POST DONNÉES CAPTEURS
# =============================================================================

Write-Host 'Analyse envoi POST donnees capteurs...' -ForegroundColor Yellow

# Patterns POST (v11.182: alignés firmware actuel [Sync]/[HTTP] + rétro [PR])
$postStarts = $lines | Select-String -Pattern '\[Sync\].*envoi POST|\[PR\] === DÉBUT POSTRAW ==='
$postSuccess = $lines | Select-String -Pattern '\[HTTP\] Requête:.*succès=oui|\[PR\] Primary server result: SUCCESS|\[PR\] Final result: SUCCESS'
$postFailed = $lines | Select-String -Pattern '\[HTTP\] Requête:.*succès=non|\[HTTP\] Erreur \d|\[PR\] Primary server result: FAILED|\[PR\] Final result: FAILED'
$postDurations = $lines | Select-String -Pattern '\[HTTP\] Requête: (\d+) ms|\[PR\] Durée totale: (\d+) ms'
$syncQueue = $lines | Select-String -Pattern '\[Sync\] Queue|DataQueue.*push|Queue.*pleine'
$postMutexError = $lines | Select-String -Pattern "\[PR\] Impossible acquerir mutex TLS"

# Calculer durées moyennes (\[HTTP\] Requête: 5708 ms ou \[PR\] Durée totale: 123 ms)
$durations = @()
foreach ($match in $postDurations) {
    if ($match.Line -match '(\d+)\s*ms') {
        $durations += [int]$matches[1]
    }
}
$avgDuration = if ($durations.Count -gt 0) { [math]::Round(($durations | Measure-Object -Average).Average, 2) } else { 0 }
$maxDuration = if ($durations.Count -gt 0) { ($durations | Measure-Object -Maximum).Maximum } else { 0 }
$minDuration = if ($durations.Count -gt 0) { ($durations | Measure-Object -Minimum).Minimum } else { 0 }

# Vérifier fréquence (attendu: toutes les 2 minutes = 120 secondes)
$postTimestamps = @()
foreach ($line in $lines) {
    if ($line -match '\[Sync\].*envoi POST|\[PR\] === DÉBUT POSTRAW ===') {
        if ($line -match '(\d{4}-\d{2}-\d{2}[\s:]\d{2}:\d{2}:\d{2})') {
            try { $postTimestamps += [DateTime]::Parse($matches[1]) } catch {}
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
$postEndpoints = $lines | Select-String -Pattern 'post-data-test|post-data[^-]'
$endpointTest = ($postEndpoints | Where-Object { $_.Line -match "post-data-test" }).Count
$endpointProd = ($postEndpoints | Where-Object { $_.Line -match "post-data[^-]" -and $_.Line -notmatch "post-data-test" }).Count

# =============================================================================
# 2. ANALYSE RÉCEPTION GET COMMANDES SERVEUR
# =============================================================================

Write-Host 'Analyse reception GET commandes serveur...' -ForegroundColor Yellow

# GET: une tentative = une ligne contenant "outputs/state: code=" (v11.186: décompte correct,
# insensible aux codes ANSI dans le log ; une seule ligne firmware par GET)
$getFetch = $lines | Select-String -Pattern 'outputs/state:\s*code='
# Parsing réussi = une ligne "GET outputs/state: body=... bytes" (HTTP) ou "Utilisation cache NVS" (fallback)
$jsonParseSuccess = $lines | Select-String -Pattern 'GET outputs/state: body=\d+ bytes|Utilisation cache NVS|\[GPIOParser\].*PARSING.*SERVEUR'
$jsonParseErrors = $lines | Select-String -Pattern '\[HTTP\] JSON parse error|JSON parse error|JSON.*fail|Parse.*error'
$getEndpoints = $lines | Select-String -Pattern "outputs-test/state|outputs/state"
$ackSent = $lines | Select-String -Pattern "ack_command|ACK.*envoyé"

# Vérifier fréquence GET (attendu: ~12s, pattern aligné sur $getFetch)
$getTimestamps = @()
foreach ($line in $lines) {
    if ($line -match 'outputs/state:\s*code=') {
        if ($line -match '(\d{4}-\d{2}-\d{2}[\s:]\d{2}:\d{2}:\d{2})') {
            try { $getTimestamps += [DateTime]::Parse($matches[1]) } catch {}
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

Write-Host "[HB] Analyse heartbeat..." -ForegroundColor Yellow

$hbStarts = $lines | Select-String -Pattern '\[HB\] === DÉBUT HEARTBEAT ==='
$hbSuccess = $lines | Select-String -Pattern '\[HB\] Succès: OUI'
$hbFailed = $lines | Select-String -Pattern '\[HB\] Succès: NON'
$hbDurations = $lines | Select-String -Pattern '\[HB\] Durée totale: (\d+) ms'

$hbDurationsList = @()
foreach ($match in $hbDurations) {
    if ($match.Line -match '(\d+) ms') {
        $hbDurationsList += [int]$matches[1]
    }
}
$avgHbDuration = if ($hbDurationsList.Count -gt 0) { [math]::Round(($hbDurationsList | Measure-Object -Average).Average, 2) } else { 0 }

# =============================================================================
# 4. ANALYSE GESTION ERREURS RÉSEAU
# =============================================================================

Write-Host 'Analyse gestion erreurs reseau...' -ForegroundColor Yellow

$wifiDisconnect = $lines | Select-String -Pattern "WiFi.*disconnect|WiFi.*deconnect|Connexion.*perdue"
$wifiConnect = $lines | Select-String -Pattern "WiFi.*connect|WiFi.*connected|Connexion.*reussie"
$tlsErrors = $lines | Select-String -Pattern "TLS.*error|SSL.*error|TLS.*fail"
$httpErrors = $lines | Select-String -Pattern "HTTP.*error|HTTP.*fail|HTTP.*timeout"
$timeouts = $lines | Select-String -Pattern "Timeout|TIMEOUT|timeout"
$queueReplay = $lines | Select-String -Pattern "replayQueuedData|Queue.*replay|Replay.*queue"

# =============================================================================
# 5. VÉRIFICATIONS DE COHÉRENCE AVEC LE CODE
# =============================================================================

Write-Host 'Verifications de coherence avec le code...' -ForegroundColor Yellow

$coherence = @()

# POST: Frequence attendue 120 secondes (REMOTE_SEND_INTERVAL_MS)
if ($avgInterval -gt 0) {
    if ($avgInterval -ge 100 -and $avgInterval -le 140) {
        $coherence += '[OK] POST: Frequence coherente (~' + $avgInterval + ' s, attendu: 120s)'
    } else {
        $coherence += '[WARN] POST: Frequence anormale (' + $avgInterval + ' s, attendu: 120s)'
    }
} else {
    $coherence += '[KO] POST: Impossible de calculer la frequence'
}

# GET: Frequence attendue 12 secondes (REMOTE_FETCH_INTERVAL_MS)
if ($avgGetInterval -gt 0) {
    if ($avgGetInterval -ge 8 -and $avgGetInterval -le 20) {
        $coherence += '[OK] GET: Frequence coherente (~' + $avgGetInterval + ' s, attendu: 12s)'
    } else {
        $coherence += '[WARN] GET: Frequence anormale (' + $avgGetInterval + ' s, attendu: 12s)'
    }
} else {
    $coherence += '[KO] GET: Impossible de calculer la frequence'
}

# POST: Duree < 5 secondes (REQUEST_TIMEOUT_MS = 5000)
if ($maxDuration -gt 0) {
    if ($maxDuration -lt 5000) {
        $coherence += '[OK] POST: Durees dans les limites (max: ' + $maxDuration + ' ms, limite: 5000 ms)'
    } else {
        $coherence += '[WARN] POST: Durees trop longues (max: ' + $maxDuration + ' ms, limite: 5000 ms)'
    }
}

# Endpoint test utilise
if ($endpointTest -gt 0) {
    $coherence += '[OK] Endpoint: Utilisation endpoint test (/ffp3/post-data-test)'
} elseif ($endpointProd -gt 0) {
    $coherence += '[WARN] Endpoint: Utilisation endpoint prod au lieu de test'
} else {
    $coherence += '[KO] Endpoint: Aucun endpoint detecte'
}

# =============================================================================
# 6. GÉNÉRATION DU RAPPORT
# =============================================================================

$dateFmt = 'yyyy-MM-dd HH:mm:ss'
$report = @(
  '=== DIAGNOSTIC ECHANGES SERVEUR DISTANT ===',
  'Date: ' + (Get-Date -Format $dateFmt),
  'Fichier log: ' + $LogFile,
  '',
  '=== 1. ENVOI POST DONNEES CAPTEURS ===',
  '',
  'Statistiques:',
  '- Debuts POST detectes: ' + $postStarts.Count,
  '- POST reussis: ' + $postSuccess.Count,
  '- POST echoues: ' + $postFailed.Count,
  '- Taux de succes: ' + $(if ($postStarts.Count -gt 0) { [math]::Round(($postSuccess.Count / $postStarts.Count) * 100, 2) } else { 0 }) + '%',
  '',
  'Performance:',
  '- Duree moyenne: ' + $avgDuration + ' ms',
  '- Duree min: ' + $minDuration + ' ms',
  '- Duree max: ' + $maxDuration + ' ms',
  '- Intervalle moyen: ' + $avgInterval + ' secondes (attendu: 120s)',
  '',
  'Endpoints:',
  '- Utilisation endpoint test: ' + $endpointTest,
  '- Utilisation endpoint prod: ' + $endpointProd,
  '',
  'Queue persistante:',
  '- Utilisations queue: ' + $syncQueue.Count,
  '- Erreurs mutex TLS: ' + $postMutexError.Count,
  '',
  '=== 2. RECEPTION GET COMMANDES SERVEUR ===',
  '',
  'Statistiques:',
  '- Tentatives GET: ' + $getFetch.Count,
  '- Parsing JSON reussis: ' + $jsonParseSuccess.Count,
  '- Erreurs parsing JSON: ' + $jsonParseErrors.Count,
  '- Taux de succes parsing: ' + $(if ($getFetch.Count -gt 0) { [math]::Round(($jsonParseSuccess.Count / $getFetch.Count) * 100, 2) } else { 0 }) + '%',
  '',
  'Performance:',
  '- Intervalle moyen: ' + $avgGetInterval + ' secondes (attendu: 12s)',
  '',
  'Endpoints:',
  '- Utilisation endpoint test: ' + (($getEndpoints | Where-Object { $_.Line -match 'outputs-test' }).Count),
  '- Utilisation endpoint prod: ' + (($getEndpoints | Where-Object { $_.Line -match 'outputs/state' -and $_.Line -notmatch 'outputs-test' }).Count),
  '',
  'ACK:',
  '- ACK envoyes: ' + $ackSent.Count,
  '',
  '=== 3. HEARTBEAT ===',
  '',
  'Statistiques:',
  '- Heartbeats detectes: ' + $hbStarts.Count,
  '- Heartbeats reussis: ' + $hbSuccess.Count,
  '- Heartbeats echoues: ' + $hbFailed.Count,
  '- Taux de succes: ' + $(if ($hbStarts.Count -gt 0) { [math]::Round(($hbSuccess.Count / $hbStarts.Count) * 100, 2) } else { 0 }) + '%',
  '',
  'Performance:',
  '- Duree moyenne: ' + $avgHbDuration + ' ms',
  '',
  '=== 4. GESTION ERREURS RESEAU ===',
  '',
  'WiFi:',
  '- Deconnexions: ' + $wifiDisconnect.Count,
  '- Reconnexions: ' + $wifiConnect.Count,
  '',
  'Erreurs:',
  '- Erreurs TLS: ' + $tlsErrors.Count,
  '- Erreurs HTTP: ' + $httpErrors.Count,
  '- Timeouts: ' + $timeouts.Count,
  '',
  'Queue:',
  '- Replays queue: ' + $queueReplay.Count,
  '',
  '=== 5. VERIFICATIONS DE COHERENCE AVEC LE CODE ===',
  '',
  ($coherence -join [Environment]::NewLine),
  '',
  '=== RESUME ===',
  '',
  'POST:',
  '- ' + $(if ($postStarts.Count -gt 0 -and ($postSuccess.Count / $postStarts.Count) -gt 0.8) { '[OK]' } elseif ($postStarts.Count -gt 0) { '[WARN] Taux de succes faible' } else { '[KO] Aucun POST detecte' }),
  '',
  'GET:',
  '- ' + $(if ($getFetch.Count -gt 0 -and ($jsonParseErrors.Count / $getFetch.Count) -lt 0.1) { '[OK]' } elseif ($getFetch.Count -gt 0) { '[WARN] Erreurs parsing frequentes' } else { '[KO] Aucun GET detecte' }),
  '',
  'Heartbeat:',
  '- ' + $(if ($hbStarts.Count -gt 0 -and ($hbSuccess.Count / $hbStarts.Count) -gt 0.8) { '[OK]' } elseif ($hbStarts.Count -gt 0) { '[WARN] Taux de succes faible' } else { '[INFO] Aucun heartbeat detecte' }),
  '',
  'Reseau:',
  '- ' + $(if ($wifiDisconnect.Count -eq 0) { '[OK] WiFi stable' } else { '[WARN] Deconnexions WiFi detectees' }),
  '',
  '=== FIN DU DIAGNOSTIC ==='
) -join ([Environment]::NewLine)

$report | Out-File -FilePath $outputFile -Encoding UTF8

Write-Host ""
Write-Host ('[OK] Diagnostic sauvegarde: ' + $outputFile) -ForegroundColor Green
Write-Host ""
Write-Host '=== RESUME ===' -ForegroundColor Cyan
Write-Host ('POST: ' + $postStarts.Count + ' tentatives, ' + $postSuccess.Count + ' reussis, ' + $postFailed.Count + ' echoues') -ForegroundColor White
Write-Host ('GET: ' + $getFetch.Count + ' tentatives, ' + $jsonParseSuccess.Count + ' parsing reussis, ' + $jsonParseErrors.Count + ' erreurs') -ForegroundColor White
Write-Host ('Heartbeat: ' + $hbStarts.Count + ' tentatives, ' + $hbSuccess.Count + ' reussis') -ForegroundColor White
Write-Host ""
