# Script d'analyse du log de monitoring
param(
    [string]$logFile = ""
)

# Si pas de fichier fourni, chercher le plus récent monitor_*.log
if ([string]::IsNullOrEmpty($logFile)) {
    $logFile = Get-ChildItem -Filter "monitor_*.log" -ErrorAction SilentlyContinue |
               Sort-Object LastWriteTime -Descending |
               Select-Object -First 1 -ExpandProperty FullName
    if (-not $logFile) {
        Write-Host "❌ Aucun fichier monitor_*.log trouvé. Spécifiez -logFile <fichier>" -ForegroundColor Red
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

# Extraire les timestamps
$firstTimestamp = ""
$lastTimestamp = ""
foreach ($line in $lines) {
    if ($line -match '(\d{2}:\d{2}:\d{2})') {
        if (-not $firstTimestamp) {
            $firstTimestamp = $matches[1]
        }
        $lastTimestamp = $matches[1]
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

"@

Write-Host "Analyse des patterns..." -ForegroundColor Yellow

# 1. Parsing JSON GET
$jsonParseSuccess = ([regex]::Matches($logContent, "JSON parsed successfully|Nettoyé préfixe chunked", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
$jsonParseErrors = ([regex]::Matches($logContent, "JSON parse error", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count

# 2. Envois POST
$postSends = ([regex]::Matches($logContent, "\[PR\]|POST.*sent|\[Sync\].*envoi POST|POST.*réussi", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count
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
