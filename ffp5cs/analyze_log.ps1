# Script d'analyse du log de monitoring
param(
    [string]$logFile = ""
)

# Si pas de fichier fourni, chercher le plus récent monitor_*.log dans logs/ puis racine
if ([string]::IsNullOrEmpty($logFile)) {
    $logsDir = Join-Path $PSScriptRoot "logs"
    $candidates = @()
    if (Test-Path $logsDir) {
        $candidates = @(Get-ChildItem -Path $logsDir -Filter "monitor_*.log" -ErrorAction SilentlyContinue)
    }
    $candidates += @(Get-ChildItem -Path $PSScriptRoot -Filter "monitor_*.log" -ErrorAction SilentlyContinue)
    $latest = $candidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $logFile = if ($latest) { $latest.FullName } else { $null }
    if (-not $logFile) {
        Write-Host "❌ Aucun fichier monitor_*.log trouvé (logs/ ou racine). Spécifiez -logFile <fichier>" -ForegroundColor Red
        exit 1
    }
}

Write-Host "=== ANALYSE DU LOG ===" -ForegroundColor Cyan
Write-Host "Fichier: $logFile" -ForegroundColor Yellow
Write-Host ""

if (-not (Test-Path $logFile)) {
    Write-Host "❌ Fichier de log introuvable: $logFile" -ForegroundColor Red
    exit 1
}

$logContent = Get-Content $logFile -Raw -ErrorAction SilentlyContinue
if (-not $logContent) {
    Write-Host "❌ Le fichier de log est vide" -ForegroundColor Red
    exit 1
}

$lines = Get-Content $logFile -ErrorAction SilentlyContinue
$lineCount = if ($lines) { $lines.Count } else { 0 }
$fileSize = (Get-Item $logFile).Length / 1KB

# Extraire les timestamps (toutes lignes pour durée effective)
$firstTimestamp = ""
$lastTimestamp = ""
$lastDeviceTimestamp = ""   # Dernière activité device (hors lignes #)
foreach ($line in $lines) {
    if ($line -match '(\d{2}:\d{2}:\d{2})') {
        if (-not $firstTimestamp) {
            $firstTimestamp = $matches[1]
        }
        $lastTimestamp = $matches[1]
        # Dernière activité device : exclure les lignes de commentaire du script (# Monitoring terminé...)
        if ($line -notmatch '^\s*#') {
            $lastDeviceTimestamp = $matches[1]
        }
    }
}

# Détection log incomplet : écart entre dernière activité device et fin de capture
$captureEndTimestamp = ""
$incompleteLogWarning = ""
foreach ($line in $lines) {
    if ($line -match '# Monitoring terminé à (\d{4}-\d{2}-\d{2}) (\d{2}:\d{2}:\d{2})') {
        $captureEndTimestamp = $matches[2]   # HH:mm:ss
        break
    }
}
if ($lastDeviceTimestamp -and $captureEndTimestamp) {
    try {
        $tLast = [DateTime]::ParseExact($lastDeviceTimestamp, "HH:mm:ss", $null)
        $tEnd = [DateTime]::ParseExact($captureEndTimestamp, "HH:mm:ss", $null)
        if ($tEnd -lt $tLast) { $tEnd = $tEnd.AddDays(1) }
        $gapMinutes = [math]::Round(($tEnd - $tLast).TotalMinutes, 1)
        if ($gapMinutes -gt 5) {
            $incompleteLogWarning = "Log possiblement incomplet : derniere activite device a $lastDeviceTimestamp, fin de capture a $captureEndTimestamp (ecart ${gapMinutes} min). POST/reboot/OTA peuvent avoir eu lieu hors fenetre."
        }
    } catch {
        # Ignorer erreur de parsing
    }
}

# Calculer la durée
$duration = "Inconnue"
if ($firstTimestamp -and $lastTimestamp) {
    try {
        $start = [DateTime]::ParseExact($firstTimestamp, "HH:mm:ss", $null)
        $end = [DateTime]::ParseExact($lastTimestamp, "HH:mm:ss", $null)
        if ($end -lt $start) {
            $end = $end.AddDays(1)
        }
        $durationSeconds = ($end - $start).TotalSeconds
        $durationMinutes = [math]::Floor($durationSeconds / 60)
        $durationSeconds = $durationSeconds % 60
        $duration = "$durationMinutes min $([math]::Round($durationSeconds)) sec"
    } catch {
        $duration = "Erreur calcul"
    }
}

$analysisFile = $logFile -replace '\.log$', '_analysis.txt'

# Extraire la version du log (pattern BOOT FFP5CS v11.xxx)
$fwVersion = "voir log"
if ($logContent -match 'BOOT FFP5CS (v\d+\.\d+)') {
    $fwVersion = $matches[1]
}

$analysis = @"
=== RAPPORT D'ANALYSE - MONITORING ESP32 ===
Version firmware: $fwVersion
Date analyse: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
Fichier log: $logFile

=== STATISTIQUES GÉNÉRALES ===
- Total lignes loggées: $lineCount
- Taille du fichier: $([math]::Round($fileSize, 2)) KB
- Première timestamp: $firstTimestamp
- Dernière timestamp: $lastTimestamp
- Durée effective: $duration
- Durée attendue: 15 minutes (900 secondes)
- $(if ($duration -match "(\d+) min") { $actualMin = [int]$matches[1]; if ($actualMin -lt 15) { "⚠️ Monitoring incomplet - seulement $actualMin minutes sur 15" } else { "✅ Monitoring complet" } } else { "⚠️ Durée non calculable" })
$(if ($incompleteLogWarning) { "`n- ⚠️ $incompleteLogWarning" } else { "" })
$(if ($logContent -match 'Connexion serie perdue|capture interrompue') { "`n- ⚠️ Capture interrompue (deconnexion serie detectee dans le log)" } else { "" })

"@

Write-Host "Analyse des patterns..." -ForegroundColor Yellow

# 1. Parsing JSON GET
$jsonParseSuccess = ([regex]::Matches($logContent, "\[GPIOParser\].*PARSING.*SERVEUR|\[HTTP\] Utilisation cache NVS", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
$jsonParseErrors = ([regex]::Matches($logContent, "\[HTTP\] JSON parse error|JSON parse error", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count

# 2. Envois POST (P3: inclure [HTTP] POST http://... émis par le firmware)
$postSends = ([regex]::Matches($logContent, "\[PR\]|POST.*sent|\[Sync\].*envoi POST|\[HTTP\].*POST|POST.*réussi", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
$postDiagnostic = ([regex]::Matches($logContent, "\[Sync\] Diagnostic|\[PR\] === DÉBUT POSTRAW", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
$postErrors = ([regex]::Matches($logContent, "POST.*échec|POST.*error|POST.*failed", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count

# 3. Monitoring mémoire
$heapChecks = ([regex]::Matches($logContent, "Heap.*libre|Heap.*minimum|\[autoTask\].*Heap|CRITICAL.*Heap|Free heap", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
$lowHeapWarnings = ([regex]::Matches($logContent, "CRITICAL.*Heap|WARN.*Heap.*faible|heap.*low|Heap.*critique", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count

# 4. DHT22
$dht22Disabled = ([regex]::Matches($logContent, "DHT22 désactivé|sensorDisabled|Capteur.*désactivé", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
$dht22Failures = ([regex]::Matches($logContent, "AirSensor.*échec|DHT.*non détecté|Capteur DHT.*déconnecté", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count

# 5. Queue capteurs (Sensor = g_sensorQueue 5 slots; DataQueue = sync payloads)
$queueFullErrors = ([regex]::Matches($logContent, "Queue pleine|queue.*full", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count

# 5b. Diagnostic WiFi / netTask / timeout (branche data vs timeout, netRPC, WiFi)
$branchData = ([regex]::Matches($logContent, '"branch"\s*:\s*"data"', [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
$branchTimeout = ([regex]::Matches($logContent, '"branch"\s*:\s*"timeout"', [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
$netRpcTimeouts = ([regex]::Matches($logContent, '\[netRPC\]\s*Timeout', [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
$wifiDisconnect = ([regex]::Matches($logContent, "WiFi.*disconnect|WiFi.*deconnect|Connexion.*perdue|\[HTTP\] WiFi (déconnecté|perdu)", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
$wifiConnect = ([regex]::Matches($logContent, "\[WiFi\]\s*OK|WiFi.*connect.*OK|Connexion.*reussie|\[Event\] WiFi.*connect", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
$getOutputsState = ([regex]::Matches($logContent, "outputs/state:\s*code=", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
$syncEnvoiPost = ([regex]::Matches($logContent, "\[Sync\].*envoi POST", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
# Demande distante recherche OTA (triggerOtaCheck reçu ou traitée par netTask)
$otaCheckRequested = ([regex]::Matches($logContent, "triggerOtaCheck|demande vérification OTA|Demande distante recherche OTA", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
$totalBranch = $branchData + $branchTimeout
$ratioDataPct = if ($totalBranch -gt 0) { [math]::Round(100.0 * $branchData / $totalBranch, 1) } else { 0 }

# 6. Erreurs critiques — Reboot = uniquement lignes rst:0x... (raison reset ESP32), pas "Hard resetting" ni "Disconnected"
$bootLines = [regex]::Matches($logContent, "rst:0x[0-9a-f]+?\s*\(", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
$bootCount = $bootLines.Count
$swCpuResetCount = ([regex]::Matches($logContent, "rst:0xc\s*\(SW_CPU_RESET\)", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
$powerOnCount = ([regex]::Matches($logContent, "rst:0x1\s*\(POWERON_RESET\)", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
$wdtCount = ([regex]::Matches($logContent, "rst:0x[0-9a-f]+.*(Watchdog|wdt|WDT)", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count

$criticalPatterns = @(
    @{ Pattern = "Guru Meditation"; Name = "Guru Meditation" },
    @{ Pattern = "panic'ed|panic\s*\("; Name = "Panic" },
    @{ Pattern = "Brownout"; Name = "Brownout" },
    @{ Pattern = "Stack overflow|STACK OVERFLOW"; Name = "Stack Overflow" }
)

$criticalIssues = @()
foreach ($check in $criticalPatterns) {
    $matches = [regex]::Matches($logContent, $check.Pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if ($matches.Count -gt 0) {
        $criticalIssues += "  ❌ $($check.Name): $($matches.Count) occurrence(s)"
    }
}
# Reboot: chaque ligne rst:0x... = un boot. SW_CPU_RESET (rst:0xc) = reset logiciel/watchdog = reboot non voulu
$rebootsAfterFirst = if ($bootCount -gt 0) { $bootCount - 1 } else { 0 }
$unwantedReboots = $rebootsAfterFirst
if ($swCpuResetCount -gt 0 -and $unwantedReboots -eq 0) { $unwantedReboots = $swCpuResetCount }
if ($unwantedReboots -gt 0) {
    $criticalIssues += "  ❌ Reboot(s) non voulu(s): $unwantedReboots (boots rst:0x = $bootCount, SW_CPU_RESET = $swCpuResetCount)"
}

# 7. Warnings
$warnings = ([regex]::Matches($logContent, "\[W\]|WARN|WARNING", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
$errors = ([regex]::Matches($logContent, "\[E\]|ERROR|ERREUR", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count

$analysis += @"
=== VÉRIFICATION DES CORRECTIONS ($fwVersion) ===

1. Parsing JSON GET:
   - Parsing réussis: $jsonParseSuccess
   - Erreurs de parsing: $jsonParseErrors
   - $(if ($jsonParseErrors -eq 0) { "✅ OK - Aucune erreur de parsing" } else { "⚠️ WARNING - $jsonParseErrors erreur(s) de parsing détectée(s)" })

2. Envois POST serveur distant:
   - Envois POST détectés: $postSends
   - Erreurs POST: $postErrors
   - Logs de diagnostic: $postDiagnostic
   - $(if ($postSends -gt 0) { "✅ OK - Envois POST effectués ($postSends)" } else { "❌ PROBLÈME - Aucun envoi POST détecté" })
   - $(if ($postErrors -gt 0) { "⚠️ WARNING - $postErrors erreur(s) POST détectée(s)" } else { "✅ OK - Aucune erreur POST" })

3. Monitoring mémoire:
   - Vérifications heap: $heapChecks
   - Alertes mémoire faible: $lowHeapWarnings
   - $(if ($lowHeapWarnings -eq 0) { "✅ OK - Aucune alerte mémoire" } else { "⚠️ WARNING - $lowHeapWarnings alerte(s) mémoire détectée(s)" })

4. DHT22 gestion échecs:
   - Désactivation détectée: $dht22Disabled
   - Échecs DHT22: $dht22Failures
   - $(if ($dht22Disabled -gt 0) { "✅ OK - Désactivation automatique fonctionnelle" } elseif ($dht22Failures -lt 10) { "✅ OK - Pas assez d'échecs pour désactivation ($dht22Failures)" } else { "⚠️ WARNING - Beaucoup d'échecs ($dht22Failures) mais pas de désactivation" })

5. Queue capteurs (g_sensorQueue 5 slots):
   - Erreurs queue pleine: $queueFullErrors
   - $(if ($queueFullErrors -eq 0) { "✅ OK - Aucune erreur de queue" } else { "⚠️ ATTENDU - $queueFullErrors erreur(s) (queue non agrandie)" })
   - Explication: la tâche automationTask consomme les lectures (xQueueReceive). Si elle est bloquée ou lente (HTTP long, affichage, etc.), sensorTask remplit la queue (5 entrées). Quand elle est pleine, la plus ancienne est écrasée (comportement voulu).

=== BILAN DIAGNOSTIC (WiFi / netTask / timeout) ===
- Branche data (données capteurs reçues): $branchData
- Branche timeout (queue capteurs vide): $branchTimeout
- Ratio data: $ratioDataPct % (sur $totalBranch cycles)
- Timeouts netRPC (appelant a abandonné en attente netTask): $netRpcTimeouts
- WiFi: $wifiConnect reconnexion(s) / événement(s) OK, $wifiDisconnect déconnexion(s) / perte(s)
- GET outputs/state exécutés (lignes code=): $getOutputsState
- Départs POST [Sync] envoi POST: $syncEnvoiPost
- Demande(s) distante(s) recherche OTA (triggerOtaCheck): $otaCheckRequested
$(if ($netRpcTimeouts -gt 0) { "- ⚠️ Des requêtes réseau ont été abandonnées (timeout appelant) - netTask peut être saturée." } else { "" })
$(if ($branchTimeout -gt 0 -and $totalBranch -gt 0 -and $ratioDataPct -lt 20) { "- ⚠️ Peu de cycles avec données capteurs ($ratioDataPct %) - timeouts capteurs fréquents." } else { "" })

=== BOOTS (rst:0x = raison reset ESP32) ===
- Lignes rst:0x... (boots): $bootCount
- Reboot(s) après 1er démarrage: $rebootsAfterFirst
- SW_CPU_RESET (logiciel/watchdog): $swCpuResetCount

=== ERREURS ET WARNINGS ===
- Warnings détectés: $warnings
- Erreurs détectées: $errors

"@

if ($criticalIssues.Count -eq 0) {
    $analysis += "✅ Aucune erreur critique détectée`n`n"
} else {
    $analysis += "`n❌ ERREURS CRITIQUES DÉTECTÉES:`n" + ($criticalIssues -join "`n") + "`n`n"
}

# Résumé final
$analysis += "=== RÉSUMÉ FINAL ===`n"
if ($criticalIssues.Count -eq 0 -and $postSends -gt 0 -and $jsonParseErrors -eq 0) {
    $analysis += "✅ SYSTÈME STABLE - Corrections efficaces`n"
    $status = "STABLE"
} elseif ($criticalIssues.Count -gt 0) {
    $analysis += "❌ SYSTÈME INSTABLE - Erreurs critiques détectées`n"
    $status = "INSTABLE"
} else {
    $analysis += "⚠️ SYSTÈME À SURVEILLER - Certaines corrections nécessitent vérification`n"
    $status = "À SURVEILLER"
}

$analysis += "`n=== FIN DE L'ANALYSE ===`n"

# Sauvegarder l'analyse
$analysis | Out-File -FilePath $analysisFile -Encoding UTF8
Write-Host "✅ Analyse sauvegardée: $analysisFile" -ForegroundColor Green
Write-Host ""
Write-Host "=== RÉSUMÉ ===" -ForegroundColor Cyan
Write-Host "Statut: $status" -ForegroundColor $(if ($status -eq "STABLE") { "Green" } elseif ($status -eq "INSTABLE") { "Red" } else { "Yellow" })
Write-Host "Log: $logFile" -ForegroundColor Gray
Write-Host "Analyse: $analysisFile" -ForegroundColor Gray
Write-Host "Lignes: $lineCount" -ForegroundColor Gray
Write-Host "Taille: $([math]::Round($fileSize, 2)) KB" -ForegroundColor Gray
Write-Host "Durée: $duration" -ForegroundColor Gray
