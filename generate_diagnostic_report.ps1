# =============================================================================
# Script de génération du rapport de diagnostic complet
# =============================================================================
# Description:
#   Combine toutes les analyses (serveur distant, mails, exhaustive) pour
#   générer un rapport Markdown complet.
#
# Usage:
#   .\generate_diagnostic_report.ps1 -LogFile "monitor_wroom_test_*.log"
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
        Write-Host 'Aucun fichier de log trouve' -ForegroundColor Red
        exit 1
    }
}

Write-Host '=== GENERATION RAPPORT DE DIAGNOSTIC COMPLET ===' -ForegroundColor Green
Write-Host ('Fichier log: ' + $LogFile) -ForegroundColor Cyan
Write-Host ""

if (-not (Test-Path $LogFile)) {
    Write-Host ('Fichier non trouve: ' + $LogFile) -ForegroundColor Red
    exit 1
}

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"

# =============================================================================
# 1. EXÉCUTER LES ANALYSES
# =============================================================================

Write-Host 'Execution des analyses...' -ForegroundColor Yellow

# Analyse serveur distant
Write-Host "  - Diagnostic serveur distant..." -ForegroundColor Gray
$serverDiagFile = 'diagnostic_serveur_distant_' + $timestamp + '.txt'
& .\diagnostic_serveur_distant.ps1 -LogFile $LogFile | Out-Null
if (Test-Path $serverDiagFile) {
    $serverDiagContent = Get-Content $serverDiagFile -Raw
} else {
    # Chercher le dernier fichier généré
    $serverDiagFile = Get-ChildItem -Filter "diagnostic_serveur_distant_*.txt" | 
                     Sort-Object LastWriteTime -Descending | 
                     Select-Object -First 1 -ExpandProperty FullName
    if ($serverDiagFile) {
        $serverDiagContent = Get-Content $serverDiagFile -Raw
    } else {
        $serverDiagContent = 'Analyse serveur distant non disponible'
    }
}

# Analyse mails (resilient: continue si script echoue, ex. encodage)
Write-Host "  - Analyse mails..." -ForegroundColor Gray
$emailDiagFile = 'check_emails_' + $timestamp + '.txt'
$emailDiagContent = ''
try {
    & .\check_emails_from_log.ps1 -LogFile $LogFile *> $emailDiagFile 2>&1
    if (Test-Path $emailDiagFile) {
        $emailDiagContent = Get-Content $emailDiagFile -Raw -ErrorAction SilentlyContinue
    }
} catch {
    # Ignore: parse error (encodage/accents dans check_emails_from_log.ps1)
}
if (-not $emailDiagContent) {
    $emailDiagContent = 'Analyse mails non disponible (script echoue ou encodage)'
}

# Analyse exhaustive
Write-Host "  - Analyse exhaustive..." -ForegroundColor Gray
$exhaustiveDiagFile = 'analyze_exhaustive_' + $timestamp + '.txt'
& .\analyze_log_exhaustive.ps1 -LogFile $LogFile *> $exhaustiveDiagFile
if (Test-Path $exhaustiveDiagFile) {
    $exhaustiveDiagContent = Get-Content $exhaustiveDiagFile -Raw
} else {
    $exhaustiveDiagContent = 'Analyse exhaustive non disponible'
}

# =============================================================================
# 2. EXTRAIRE STATISTIQUES DU LOG
# =============================================================================

Write-Host 'Extraction des statistiques...' -ForegroundColor Yellow

$lines = Get-Content $LogFile
$lineCount = $lines.Count
$fileSize = (Get-Item $LogFile).Length / 1KB

# Statistiques générales
$warnings = ($lines | Select-String -Pattern "\[W\]|WARN|WARNING" -CaseSensitive:$false).Count
$errors = ($lines | Select-String -Pattern "\[E\]|ERROR|ERREUR" -CaseSensitive:$false).Count
# v11.172: Exclure diag_hasPanic (lecture NVS normale, pas un crash)
$crashes = ($lines | Select-String -Pattern "Guru Meditation|panic|abort\(\)|Stack canary" -CaseSensitive:$false | Where-Object { $_.Line -notmatch "diag_hasPanic" }).Count

# =============================================================================
# 3. GÉNÉRER LE RAPPORT MARKDOWN
# =============================================================================

Write-Host 'Generation du rapport Markdown...' -ForegroundColor Yellow

$reportFile = 'rapport_diagnostic_complet_' + $timestamp + '.md'

# Lignes listant les stats
$statsLines = '- **Total lignes loggees:** ' + $lineCount + [Environment]::NewLine + '- **Taille du fichier:** ' + [math]::Round($fileSize, 2) + ' KB' + [Environment]::NewLine + '- **Warnings:** ' + $warnings + [Environment]::NewLine + '- **Erreurs:** ' + $errors + [Environment]::NewLine + '- **Crashes:** ' + $crashes

# Section Fichiers Generes
$filesSection = '- **Log complet:** ' + $LogFile + [Environment]::NewLine + '- **Diagnostic serveur:** ' + $serverDiagFile + [Environment]::NewLine + '- **Diagnostic mails:** ' + $emailDiagFile + [Environment]::NewLine + '- **Analyse exhaustive:** ' + $exhaustiveDiagFile + [Environment]::NewLine + '- **Rapport complet:** ' + $reportFile

# Section Recommandations (construite en dehors du here-string principal)
$recommendationsSection = if ($crashes -gt 0) {
@'
### ❌ ACTION REQUISE - Crashes Détectés

1. **Analyser les core dumps** si disponibles
2. **Vérifier la configuration matérielle**
3. **Examiner les logs détaillés** pour identifier la cause
4. **Refaire un test** après corrections
'@
} elseif ($errors -gt 20) {
@'
### ⚠️ ATTENTION - Nombre d'Erreurs Élevé

1. **Analyser les erreurs** dans le log détaillé
2. **Vérifier la stabilité WiFi**
3. **Surveiller la mémoire heap**
4. **Considérer des ajustements de configuration**
'@
} else {
@'
### ✅ SYSTÈME STABLE

1. **Continuer le monitoring** en production
2. **Surveiller les métriques** mémoire et réseau
3. **Vérifier périodiquement** les échanges serveur et mail
'@
}

# Resumes serveur (evite problemes encodage guillemets)
$postSummary = if ($serverDiagContent -match 'POST:.*reussis') {
    $postMatch = [regex]::Match($serverDiagContent, 'POST reussis: (\d+)')
    $postSuccess = if ($postMatch.Success) { $postMatch.Groups[1].Value } else { 'N/A' }
    $postMatch = [regex]::Match($serverDiagContent, 'POST echoues: (\d+)')
    $postFailed = if ($postMatch.Success) { $postMatch.Groups[1].Value } else { 'N/A' }
    '- **POST:** ' + $postSuccess + ' reussis, ' + $postFailed + ' echoues'
} else { '- **POST:** Statistiques non disponibles' }
$getSummary = if ($serverDiagContent -match 'GET:') {
    $getMatch = [regex]::Match($serverDiagContent, 'Tentatives GET: (\d+)')
    $getCount = if ($getMatch.Success) { $getMatch.Groups[1].Value } else { 'N/A' }
    $getMatch = [regex]::Match($serverDiagContent, 'Erreurs parsing JSON: (\d+)')
    $getErrors = if ($getMatch.Success) { $getMatch.Groups[1].Value } else { 'N/A' }
    '- **GET:** ' + $getCount + ' tentatives, ' + $getErrors + ' erreurs parsing'
} else { '- **GET:** Statistiques non disponibles' }
$emailSummary = if ($emailDiagContent -match 'Mails attendus:') {
    $emailMatch = [regex]::Match($emailDiagContent, 'Mails attendus: (\d+)')
    $emailExpected = if ($emailMatch.Success) { $emailMatch.Groups[1].Value } else { 'N/A' }
    $emailMatch = [regex]::Match($emailDiagContent, 'Mails envoyes avec succes: (\d+)')
    $emailSent = if ($emailMatch.Success) { $emailMatch.Groups[1].Value } else { 'N/A' }
    $emailMatch = [regex]::Match($emailDiagContent, 'Mails echoues: (\d+)')
    $emailFailed = if ($emailMatch.Success) { $emailMatch.Groups[1].Value } else { 'N/A' }
    '- **Mails attendus:** ' + $emailExpected + [Environment]::NewLine + '- **Mails envoyes:** ' + $emailSent + [Environment]::NewLine + '- **Mails echoues:** ' + $emailFailed
} else { '- Statistiques mails non disponibles' }
$crashSummary = if ($exhaustiveDiagContent -match 'Crashes') {
    $crashMatch = [regex]::Match($exhaustiveDiagContent, 'Crashes.*?(\d+)')
    $crashCount = if ($crashMatch.Success) { $crashMatch.Groups[1].Value } else { '0' }
    '- **Crashes:** ' + $crashCount
} else { '- **Crashes:** Non detectes' }
$coherenceServer = if ($serverDiagContent -match 'VERIFICATIONS DE COHERENCE') {
    $coherenceSection = $serverDiagContent -split 'VERIFICATIONS DE COHERENCE' | Select-Object -Last 1
    $coherenceSection = $coherenceSection -split 'RESUME' | Select-Object -First 1
    ($coherenceSection -split [Environment]::NewLine | Where-Object { $_ -match '\[OK\]|\[WARN\]|\[KO\]' } | ForEach-Object { '- ' + $_ }) -join [Environment]::NewLine
} else { '- Verifications de coherence non disponibles' }
$coherenceMail = if ($emailDiagContent -match 'Mails manquants') {
    $missingMatch = [regex]::Match($emailDiagContent, 'MAILS MANQUANTS \((\d+)\)')
    $missingCount = if ($missingMatch.Success) { $missingMatch.Groups[1].Value } else { '0' }
    if ([int]$missingCount -gt 0) { '- ' + $missingCount + ' mail(s) manquant(s) detecte(s)' } else { '- Tous les mails attendus ont ete envoyes' }
} else { '- Verifications mails non disponibles' }
$statusGlobal = if ($crashes -eq 0 -and $errors -lt 10) { '**SYSTEME STABLE** - Aucun crash detecte, erreurs mineures' } elseif ($crashes -gt 0) { '**SYSTEME INSTABLE** - ' + $crashes + ' crash(es) detecte(s)' } else { '**SYSTEME A SURVEILLER** - ' + $errors + ' erreur(s) detectee(s)' }
$durationMonitoring = if ($lines.Count -gt 0) { 'Analyse terminee' } else { 'Non disponible' }

$report = @'
# Rapport de Diagnostic Complet - WROOM-TEST

**Date:** __DATE__
**Fichier log:** __LOGFILE__
**Durée monitoring:** __DURATION__

---

## Résumé Exécutif

### Statut Global

__STATUS_GLOBAL__

### Statistiques Générales

__STATS_LINES__

---

## 1. Diagnostic Échanges Serveur Distant

### Résumé

__POST_SUMMARY__
__GET_SUMMARY__

### Détails Complets

```
__SERVER_DIAG__
```

---

## 2. Diagnostic Échanges Mail

### Résumé

__EMAIL_SUMMARY__

### Détails Complets

```
__EMAIL_DIAG__
```

---

## 3. Analyse Exhaustive des Logs

### Résumé

__CRASH_SUMMARY__

### Détails Complets

```
__EXHAUSTIVE_DIAG__
```

---

## 4. Vérifications de Cohérence avec le Code

### Serveur Distant

__COHERENCE_SERVER__

### Mail

__COHERENCE_MAIL__

---

## 5. Recommandations

__RECOMMENDATIONS__

---

## Fichiers Générés

__FILES_SECTION__

---

*Rapport genere automatiquement par generate_diagnostic_report.ps1*
'@
$report = $report -replace '__DATE__', (Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
$report = $report -replace '__LOGFILE__', $LogFile
$report = $report -replace '__DURATION__', $durationMonitoring
$report = $report -replace '__STATUS_GLOBAL__', $statusGlobal
$report = $report -replace '__STATS_LINES__', $statsLines
$report = $report -replace '__POST_SUMMARY__', $postSummary
$report = $report -replace '__GET_SUMMARY__', $getSummary
$report = $report -replace '__SERVER_DIAG__', $serverDiagContent
$report = $report -replace '__EMAIL_SUMMARY__', $emailSummary
$report = $report -replace '__EMAIL_DIAG__', $emailDiagContent
$report = $report -replace '__CRASH_SUMMARY__', $crashSummary
$report = $report -replace '__EXHAUSTIVE_DIAG__', $exhaustiveDiagContent
$report = $report -replace '__COHERENCE_SERVER__', $coherenceServer
$report = $report -replace '__COHERENCE_MAIL__', $coherenceMail
$report = $report -replace '__RECOMMENDATIONS__', $recommendationsSection
$report = $report -replace '__FILES_SECTION__', $filesSection

$report | Out-File -FilePath $reportFile -Encoding UTF8

Write-Host ''
Write-Host 'Rapport genere:' $reportFile -ForegroundColor Green
Write-Host ''
Write-Host '=== RESUME ===' -ForegroundColor Cyan
$statusMsg = if ($crashes -eq 0 -and $errors -lt 10) { 'STABLE' } elseif ($crashes -gt 0) { 'INSTABLE' } else { 'A SURVEILLER' }
$colorMsg = if ($crashes -eq 0 -and $errors -lt 10) { 'Green' } elseif ($crashes -gt 0) { 'Red' } else { 'Yellow' }
Write-Host 'Statut:' $statusMsg -ForegroundColor $colorMsg
Write-Host 'Lignes:' $lineCount -ForegroundColor Gray
Write-Host 'Taille:' ([math]::Round($fileSize, 2)) 'KB' -ForegroundColor Gray
Write-Host 'Rapport:' $reportFile -ForegroundColor Gray
Write-Host ''
