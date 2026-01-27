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
    # Trouver le dernier fichier de log
    $LogFile = Get-ChildItem -Filter "monitor_wroom_test_*.log" | 
               Sort-Object LastWriteTime -Descending | 
               Select-Object -First 1 -ExpandProperty FullName
    
    if (-not $LogFile) {
        Write-Host "❌ Aucun fichier de log trouvé" -ForegroundColor Red
        exit 1
    }
}

Write-Host "=== GÉNÉRATION RAPPORT DE DIAGNOSTIC COMPLET ===" -ForegroundColor Green
Write-Host "Fichier log: $LogFile" -ForegroundColor Cyan
Write-Host ""

if (-not (Test-Path $LogFile)) {
    Write-Host "❌ Fichier non trouvé: $LogFile" -ForegroundColor Red
    exit 1
}

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"

# =============================================================================
# 1. EXÉCUTER LES ANALYSES
# =============================================================================

Write-Host "📊 Exécution des analyses..." -ForegroundColor Yellow

# Analyse serveur distant
Write-Host "  - Diagnostic serveur distant..." -ForegroundColor Gray
$serverDiagFile = "diagnostic_serveur_distant_$timestamp.txt"
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
        $serverDiagContent = "Analyse serveur distant non disponible"
    }
}

# Analyse mails
Write-Host "  - Analyse mails..." -ForegroundColor Gray
$emailDiagFile = "check_emails_$timestamp.txt"
& .\check_emails_from_log.ps1 -LogFile $LogFile *> $emailDiagFile
if (Test-Path $emailDiagFile) {
    $emailDiagContent = Get-Content $emailDiagFile -Raw
} else {
    $emailDiagContent = "Analyse mails non disponible"
}

# Analyse exhaustive
Write-Host "  - Analyse exhaustive..." -ForegroundColor Gray
$exhaustiveDiagFile = "analyze_exhaustive_$timestamp.txt"
& .\analyze_log_exhaustive.ps1 -LogFile $LogFile *> $exhaustiveDiagFile
if (Test-Path $exhaustiveDiagFile) {
    $exhaustiveDiagContent = Get-Content $exhaustiveDiagFile -Raw
} else {
    $exhaustiveDiagContent = "Analyse exhaustive non disponible"
}

# =============================================================================
# 2. EXTRAIRE STATISTIQUES DU LOG
# =============================================================================

Write-Host "📈 Extraction des statistiques..." -ForegroundColor Yellow

$lines = Get-Content $LogFile
$lineCount = $lines.Count
$fileSize = (Get-Item $LogFile).Length / 1KB

# Statistiques générales
$warnings = ($lines | Select-String -Pattern "\[W\]|WARN|WARNING" -CaseSensitive:$false).Count
$errors = ($lines | Select-String -Pattern "\[E\]|ERROR|ERREUR" -CaseSensitive:$false).Count
$crashes = ($lines | Select-String -Pattern "Guru Meditation|panic|abort\(\)|Stack canary" -CaseSensitive:$false).Count

# =============================================================================
# 3. GÉNÉRER LE RAPPORT MARKDOWN
# =============================================================================

Write-Host "📝 Génération du rapport Markdown..." -ForegroundColor Yellow

$reportFile = "rapport_diagnostic_complet_$timestamp.md"

$report = @"
# Rapport de Diagnostic Complet - WROOM-TEST

**Date:** $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  
**Fichier log:** \`$LogFile\`
**Durée monitoring:** $(if ($lines.Count -gt 0) { "Analyse terminée" } else { "Non disponible" })

---

## Résumé Exécutif

### Statut Global

$(if ($crashes -eq 0 -and $errors -lt 10) {
"✅ **SYSTÈME STABLE** - Aucun crash détecté, erreurs mineures"
} elseif ($crashes -gt 0) {
"❌ **SYSTÈME INSTABLE** - $crashes crash(es) détecté(s)"
} else {
"⚠️ **SYSTÈME À SURVEILLER** - $errors erreur(s) détectée(s)"
})

### Statistiques Générales

- **Total lignes loggées:** $lineCount
- **Taille du fichier:** $([math]::Round($fileSize, 2)) KB
- **Warnings:** $warnings
- **Erreurs:** $errors
- **Crashes:** $crashes

---

## 1. Diagnostic Échanges Serveur Distant

### Résumé

$(if ($serverDiagContent -match "POST:.*réussis") {
    $postMatch = [regex]::Match($serverDiagContent, "POST réussis: (\d+)")
    $postSuccess = if ($postMatch.Success) { $postMatch.Groups[1].Value } else { "N/A" }
    $postMatch = [regex]::Match($serverDiagContent, "POST échoués: (\d+)")
    $postFailed = if ($postMatch.Success) { $postMatch.Groups[1].Value } else { "N/A" }
    "- **POST:** $postSuccess réussis, $postFailed échoués"
} else {
    "- **POST:** Statistiques non disponibles"
})

$(if ($serverDiagContent -match "GET:") {
    $getMatch = [regex]::Match($serverDiagContent, "Tentatives GET: (\d+)")
    $getCount = if ($getMatch.Success) { $getMatch.Groups[1].Value } else { "N/A" }
    $getMatch = [regex]::Match($serverDiagContent, "Erreurs parsing JSON: (\d+)")
    $getErrors = if ($getMatch.Success) { $getMatch.Groups[1].Value } else { "N/A" }
    "- **GET:** $getCount tentatives, $getErrors erreurs parsing"
} else {
    "- **GET:** Statistiques non disponibles"
})

### Détails Complets

\`\`\`
$serverDiagContent
\`\`\`

---

## 2. Diagnostic Échanges Mail

### Résumé

$(if ($emailDiagContent -match "Mails attendus:") {
    $emailMatch = [regex]::Match($emailDiagContent, "Mails attendus: (\d+)")
    $emailExpected = if ($emailMatch.Success) { $emailMatch.Groups[1].Value } else { "N/A" }
    $emailMatch = [regex]::Match($emailDiagContent, "Mails envoyés avec succès: (\d+)")
    $emailSent = if ($emailMatch.Success) { $emailMatch.Groups[1].Value } else { "N/A" }
    $emailMatch = [regex]::Match($emailDiagContent, "Mails échoués: (\d+)")
    $emailFailed = if ($emailMatch.Success) { $emailMatch.Groups[1].Value } else { "N/A" }
    "- **Mails attendus:** $emailExpected
- **Mails envoyés:** $emailSent
- **Mails échoués:** $emailFailed"
} else {
    "- Statistiques mails non disponibles"
})

### Détails Complets

\`\`\`
$emailDiagContent
\`\`\`

---

## 3. Analyse Exhaustive des Logs

### Résumé

$(if ($exhaustiveDiagContent -match "Crashes") {
    $crashMatch = [regex]::Match($exhaustiveDiagContent, "Crashes.*?(\d+)")
    $crashCount = if ($crashMatch.Success) { $crashMatch.Groups[1].Value } else { "0" }
    "- **Crashes:** $crashCount"
} else {
    "- **Crashes:** Non détectés"
})

### Détails Complets

\`\`\`
$exhaustiveDiagContent
\`\`\`

---

## 4. Vérifications de Cohérence avec le Code

### Serveur Distant

$(if ($serverDiagContent -match "VÉRIFICATIONS DE COHÉRENCE") {
    $coherenceSection = $serverDiagContent -split "VÉRIFICATIONS DE COHÉRENCE" | Select-Object -Last 1
    $coherenceSection = $coherenceSection -split "RÉSUMÉ" | Select-Object -First 1
    $coherenceLines = $coherenceSection -split "`n" | Where-Object { $_ -match "✅|⚠️|❌" }
    foreach ($line in $coherenceLines) {
        "- $line"
    }
} else {
    "- Vérifications de cohérence non disponibles"
})

### Mail

$(if ($emailDiagContent -match "Mails manquants") {
    $missingMatch = [regex]::Match($emailDiagContent, "MAILS MANQUANTS \((\d+)\)")
    $missingCount = if ($missingMatch.Success) { $missingMatch.Groups[1].Value } else { "0" }
    if ([int]$missingCount -gt 0) {
        "- ⚠️ $missingCount mail(s) manquant(s) détecté(s)"
    } else {
        "- ✅ Tous les mails attendus ont été envoyés"
    }
} else {
    "- Vérifications mails non disponibles"
})

---

## 5. Recommandations

$(if ($crashes -gt 0) {
@"
### ❌ ACTION REQUISE - Crashes Détectés

1. **Analyser les core dumps** si disponibles
2. **Vérifier la configuration matérielle**
3. **Examiner les logs détaillés** pour identifier la cause
4. **Refaire un test** après corrections

"@
} elseif ($errors -gt 20) {
@"
### ⚠️ ATTENTION - Nombre d'Erreurs Élevé

1. **Analyser les erreurs** dans le log détaillé
2. **Vérifier la stabilité WiFi**
3. **Surveiller la mémoire heap**
4. **Considérer des ajustements de configuration**

"@
} else {
@"
### ✅ SYSTÈME STABLE

1. **Continuer le monitoring** en production
2. **Surveiller les métriques** mémoire et réseau
3. **Vérifier périodiquement** les échanges serveur et mail

"@
})

---

## Fichiers Générés

- **Log complet:** \`$LogFile\`
- **Diagnostic serveur:** \`$serverDiagFile\`
- **Diagnostic mails:** \`$emailDiagFile\`
- **Analyse exhaustive:** \`$exhaustiveDiagFile\`
- **Rapport complet:** \`$reportFile\`

---

*Rapport généré automatiquement par generate_diagnostic_report.ps1*
"@

$report | Out-File -FilePath $reportFile -Encoding UTF8

Write-Host ""
Write-Host "✅ Rapport généré: $reportFile" -ForegroundColor Green
Write-Host ""
Write-Host "=== RÉSUMÉ ===" -ForegroundColor Cyan
Write-Host "Statut: $(if ($crashes -eq 0 -and $errors -lt 10) { "✅ STABLE" } elseif ($crashes -gt 0) { "❌ INSTABLE" } else { "⚠️ À SURVEILLER" })" -ForegroundColor $(if ($crashes -eq 0 -and $errors -lt 10) { "Green" } elseif ($crashes -gt 0) { "Red" } else { "Yellow" })
Write-Host "Lignes: $lineCount" -ForegroundColor Gray
Write-Host "Taille: $([math]::Round($fileSize, 2)) KB" -ForegroundColor Gray
Write-Host "Rapport: $reportFile" -ForegroundColor Gray
Write-Host ""
