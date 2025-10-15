# Script de monitoring 5 minutes avec analyse mémoire et problèmes
# Date: 2025-10-14

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$logFile = "monitor_5min_$timestamp.log"
$analysisFile = "monitor_5min_analysis_$timestamp.md"

Write-Host "=== MONITORING ESP32 - 5 MINUTES ===" -ForegroundColor Cyan
Write-Host "Fichier de log: $logFile"
Write-Host "Fichier d'analyse: $analysisFile"
Write-Host ""

# Lancer le monitoring
Write-Host "Démarrage du monitoring (5 minutes)..." -ForegroundColor Yellow
pio device monitor --environment esp32-wroom-32 --filter=esp32_exception_decoder 2>&1 | Tee-Object -FilePath $logFile | ForEach-Object {
    $line = $_
    
    # Afficher avec couleurs
    if ($line -match "ERROR|PANIC|Guru Meditation|Brownout") {
        Write-Host $line -ForegroundColor Red
    } elseif ($line -match "WARNING|WARN") {
        Write-Host $line -ForegroundColor Yellow
    } elseif ($line -match "Heap|heap|MALLOC|Free:") {
        Write-Host $line -ForegroundColor Cyan
    } else {
        Write-Host $line
    }
    
    # Timeout après 5 minutes
    $script:lineCount++
    if ($script:lineCount % 100 -eq 0) {
        $elapsed = (Get-Date) - $script:startTime
        if ($elapsed.TotalSeconds -gt 300) {
            Write-Host "`nTimeout 5 minutes atteint" -ForegroundColor Green
            break
        }
    }
} -Begin { $script:lineCount = 0; $script:startTime = Get-Date }

Write-Host "`n=== ANALYSE DES LOGS ===" -ForegroundColor Cyan

# Analyse des logs
$content = Get-Content $logFile -Raw

# Analyse mémoire
$heapMatches = Select-String -Path $logFile -Pattern "Free: \d+" -AllMatches
$memoryLines = Select-String -Path $logFile -Pattern "heap|Heap|MALLOC|Free:" -CaseSensitive:$false

# Problèmes critiques
$errors = Select-String -Path $logFile -Pattern "ERROR|PANIC|Guru Meditation|Brownout|assert|Core dump" -CaseSensitive:$false
$warnings = Select-String -Path $logFile -Pattern "WARNING|WARN|Watchdog|timeout" -CaseSensitive:$false

# Créer le rapport d'analyse
$analysis = @"
# Analyse Monitoring 5 Minutes - ESP32
**Date**: $timestamp
**Durée**: 5 minutes (300 secondes)

## 🔴 PROBLÈMES CRITIQUES

### Erreurs détectées
"@

if ($errors.Count -gt 0) {
    $analysis += "`n**Nombre d'erreurs**: $($errors.Count)`n`n"
    $errors | ForEach-Object { $analysis += "- Line $($_.LineNumber): $($_.Line)`n" }
} else {
    $analysis += "`n✅ **Aucune erreur critique détectée**`n"
}

$analysis += @"

### Warnings détectés
"@

if ($warnings.Count -gt 0) {
    $analysis += "`n**Nombre de warnings**: $($warnings.Count)`n`n"
    $warnings | Select-Object -First 10 | ForEach-Object { $analysis += "- Line $($_.LineNumber): $($_.Line)`n" }
    if ($warnings.Count -gt 10) {
        $analysis += "`n... et $($warnings.Count - 10) autres warnings`n"
    }
} else {
    $analysis += "`n✅ **Aucun warning détecté**`n"
}

$analysis += @"

## 💾 GESTION MÉMOIRE

### Statistiques mémoire
"@

if ($memoryLines.Count -gt 0) {
    $analysis += "`n**Nombre de lignes mémoire**: $($memoryLines.Count)`n`n"
    
    # Extraire les valeurs de heap
    $heapValues = @()
    $heapMatches | ForEach-Object {
        if ($_.Line -match "Free: (\d+)") {
            $heapValues += [int]$matches[1]
        }
    }
    
    if ($heapValues.Count -gt 0) {
        $minHeap = ($heapValues | Measure-Object -Minimum).Minimum
        $maxHeap = ($heapValues | Measure-Object -Maximum).Maximum
        $avgHeap = [math]::Round(($heapValues | Measure-Object -Average).Average, 0)
        
        $analysis += "**Heap libre (bytes)**:`n"
        $analysis += "- Minimum: $minHeap bytes ($([math]::Round($minHeap/1024, 2)) KB)`n"
        $analysis += "- Maximum: $maxHeap bytes ($([math]::Round($maxHeap/1024, 2)) KB)`n"
        $analysis += "- Moyenne: $avgHeap bytes ($([math]::Round($avgHeap/1024, 2)) KB)`n`n"
        
        # Alerte si mémoire faible
        if ($minHeap -lt 30000) {
            $analysis += "⚠️ **ALERTE**: Mémoire libre très faible détectée (< 30KB)`n"
        } elseif ($minHeap -lt 50000) {
            $analysis += "⚠️ **ATTENTION**: Mémoire libre basse détectée (< 50KB)`n"
        } else {
            $analysis += "✅ **Mémoire stable** (> 50KB)`n"
        }
    }
    
    # Échantillon des lignes mémoire
    $analysis += "`n### Échantillon de logs mémoire (10 premiers):`n`n"
    $memoryLines | Select-Object -First 10 | ForEach-Object { $analysis += "``````Line $($_.LineNumber): $($_.Line)```````n" }
} else {
    $analysis += "`n⚠️ **Aucune information mémoire trouvée dans les logs**`n"
}

$analysis += @"

## 📊 STATISTIQUES GÉNÉRALES

- **Lignes de log totales**: $($(Get-Content $logFile).Count)
- **Erreurs critiques**: $($errors.Count)
- **Warnings**: $($warnings.Count)
- **Lignes mémoire**: $($memoryLines.Count)

## 🎯 CONCLUSION

"@

# Conclusion automatique
$criticalIssues = $false
if ($errors.Count -gt 0) {
    $analysis += "❌ **PROBLÈMES CRITIQUES DÉTECTÉS** - Action requise immédiatement`n"
    $criticalIssues = $true
} elseif ($warnings.Count -gt 5) {
    $analysis += "⚠️ **WARNINGS MULTIPLES** - Surveillance recommandée`n"
} elseif ($heapValues.Count -gt 0 -and ($heapValues | Measure-Object -Minimum).Minimum -lt 50000) {
    $analysis += "⚠️ **MÉMOIRE FAIBLE** - Optimisation recommandée`n"
} else {
    $analysis += "✅ **SYSTÈME STABLE** - Aucun problème majeur détecté`n"
}

$analysis += @"

## 📁 FICHIERS GÉNÉRÉS

- Log complet: ``$logFile``
- Analyse: ``$analysisFile``

---
*Généré automatiquement le $timestamp*
"@

# Sauvegarder l'analyse
$analysis | Out-File -FilePath $analysisFile -Encoding UTF8

# Afficher l'analyse
Write-Host "`n=== RÉSUMÉ ===" -ForegroundColor Green
Write-Host $analysis

Write-Host "`n✅ Analyse terminée!" -ForegroundColor Green
Write-Host "Fichiers générés:" -ForegroundColor Cyan
Write-Host "  - $logFile" -ForegroundColor White
Write-Host "  - $analysisFile" -ForegroundColor White

