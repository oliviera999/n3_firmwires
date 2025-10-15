# Script d'Archivage des Documents Obsolètes - Projet FFP5CS
# Version: 1.0
# Date: 2025-10-13
# Description: Archive 111 fichiers .md obsolètes dans docs/archives/

param(
    [switch]$DryRun = $false,  # Simulation sans modification
    [switch]$Verbose = $false   # Affichage détaillé
)

# Couleurs pour l'affichage
$ErrorColor = "Red"
$WarningColor = "Yellow"
$SuccessColor = "Green"
$InfoColor = "Cyan"

# Configuration
$rootDir = $PSScriptRoot
$archiveBaseDir = Join-Path $rootDir "docs\archives"

# Créer la structure des dossiers d'archives
$archiveDirs = @{
    "sessions"  = Join-Path $archiveBaseDir "sessions"
    "phase1-2"  = Join-Path $archiveBaseDir "phase1-2"
    "fixes"     = Join-Path $archiveBaseDir "fixes"
    "analyses"  = Join-Path $archiveBaseDir "analyses"
    "guides"    = Join-Path $archiveBaseDir "guides"
    "points_entree" = Join-Path $archiveBaseDir "points_entree"
    "monitoring" = Join-Path $archiveBaseDir "monitoring"
    "verification" = Join-Path $archiveBaseDir "verification"
}

# Fonction d'affichage
function Write-Status {
    param([string]$Message, [string]$Type = "Info")
    
    $color = switch ($Type) {
        "Error"   { $ErrorColor }
        "Warning" { $WarningColor }
        "Success" { $SuccessColor }
        default   { $InfoColor }
    }
    
    Write-Host $Message -ForegroundColor $color
}

# Fonction de déplacement sécurisé
function Move-FileToArchive {
    param(
        [string]$FileName,
        [string]$ArchiveCategory
    )
    
    $sourcePath = Join-Path $rootDir $FileName
    $destDir = $archiveDirs[$ArchiveCategory]
    $destPath = Join-Path $destDir $FileName
    
    # Vérifier que le fichier source existe
    if (-not (Test-Path $sourcePath)) {
        if ($Verbose) {
            Write-Status "  ⚠️  Fichier introuvable: $FileName" "Warning"
        }
        return $false
    }
    
    # Mode simulation
    if ($DryRun) {
        Write-Status "  [DRY-RUN] Déplacerait: $FileName → $ArchiveCategory\" "Info"
        return $true
    }
    
    # Créer le dossier de destination si nécessaire
    if (-not (Test-Path $destDir)) {
        New-Item -ItemType Directory -Path $destDir -Force | Out-Null
    }
    
    # Déplacer le fichier
    try {
        Move-Item -Path $sourcePath -Destination $destPath -Force
        Write-Status "  ✅ Archivé: $FileName" "Success"
        return $true
    }
    catch {
        Write-Status "  ❌ Erreur: $FileName - $_" "Error"
        return $false
    }
}

# Bannière
Write-Host ""
Write-Host "═══════════════════════════════════════════════════════════" -ForegroundColor Cyan
Write-Host "  🗂️  ARCHIVAGE DOCUMENTS OBSOLÈTES - PROJET FFP5CS" -ForegroundColor Cyan
Write-Host "═══════════════════════════════════════════════════════════" -ForegroundColor Cyan
Write-Host ""

if ($DryRun) {
    Write-Status "🔍 MODE SIMULATION ACTIVÉ (aucune modification)" "Warning"
    Write-Host ""
}

# Statistiques
$stats = @{
    Total = 0
    Success = 0
    Failed = 0
    NotFound = 0
}

# ========================================
# CATÉGORIE 1: SESSIONS OBSOLÈTES (12)
# ========================================
Write-Status "📁 CATÉGORIE 1: Sessions Obsolètes (12 fichiers)" "Info"

$sessions = @(
    "LISEZ_MOI_SESSION_2025-10-13.md",
    "SESSION_COMPLETE_V11.08_2025-10-12.md",
    "SESSION_COMPLETE_2025-10-11_PHASE2.md",
    "RESUME_SESSION_2025-10-12_CRASH_FIX.md",
    "FIN_DE_JOURNEE_2025-10-11.md",
    "JOURNEE_COMPLETE_2025-10-11.md",
    "SESSION_COMPLETE_V10.93.md",
    "SESSION_2025-10-13_PHASE2_SYNC_COMPLETE.md",
    "SESSION_CLOSE_FINALE.md",
    "SYNTHESE_FINALE_SESSION_2025-10-12.md",
    "SYNTHESE_FINALE_SESSION.md",
    "RECAPITULATIF_COMPLET_SESSION_2025-10-13.md"
)

foreach ($file in $sessions) {
    $stats.Total++
    if (Move-FileToArchive -FileName $file -ArchiveCategory "sessions") {
        $stats.Success++
    } else {
        $stats.NotFound++
    }
}

# ========================================
# CATÉGORIE 2: POINTS D'ENTRÉE MULTIPLES (6)
# ========================================
Write-Host ""
Write-Status "📁 CATÉGORIE 2: Points d'Entrée Redondants (6 fichiers)" "Info"

$pointsEntree = @(
    "LISEZ_MOI_DABORD.md",
    "A_LIRE_MAINTENANT.md",
    "A_FAIRE_MAINTENANT.md",
    "LISEZ_MOI_V11.08.md",
    "DEMARRAGE_RAPIDE.md",
    "README_ANALYSE.md",
    "README_FINAL_SESSION.md"
)

foreach ($file in $pointsEntree) {
    $stats.Total++
    if (Move-FileToArchive -FileName $file -ArchiveCategory "points_entree") {
        $stats.Success++
    } else {
        $stats.NotFound++
    }
}

# ========================================
# CATÉGORIE 3: PHASE 2 PROGRESSION (12)
# ========================================
Write-Host ""
Write-Status "📁 CATÉGORIE 3: Phase 2 Progression (12 fichiers)" "Info"

$phase2 = @(
    "PHASE_2_REFACTORING_PLAN.md",
    "PHASE_2_GUIDE_COMPLET_MODULES.md",
    "PHASE_2_PROGRESSION.md",
    "PHASE_2_ETAT_ACTUEL.md",
    "PHASE_2_COMPLETE.md",
    "PHASE_2_FINAL_PRODUCTION_READY.md",
    "PHASE_2_TERMINEE_OFFICIEL.md",
    "PHASE_2.11_COMPLETE.md",
    "PHASE_2.9_GUIDE_10_POURCENT.md",
    "PHASE2_PROGRESSION_92POURCENT.md",
    "PHASE2_COMPLETION_FINALE.md",
    "CLOTURE_SESSION_PHASE2_92.md"
)

foreach ($file in $phase2) {
    $stats.Total++
    if (Move-FileToArchive -FileName $file -ArchiveCategory "phase1-2") {
        $stats.Success++
    } else {
        $stats.NotFound++
    }
}

# ========================================
# CATÉGORIE 4: SUCCESS/FINALE/COMPLETE (11)
# ========================================
Write-Host ""
Write-Status "📁 CATÉGORIE 4: Documents Finaux Redondants (11 fichiers)" "Info"

$finaux = @(
    "SUCCESS_PHASE2_COMPLETE.md",
    "FLASH_PHASE2_SUCCESS.md",
    "MISSION_FINALE_ACCOMPLIE.md",
    "FIN_TRAVAIL_COMPLET.md",
    "TRAVAIL_COMPLET_AUJOURDHUI.md",
    "TRAVAIL_TERMINE_PHASE2.md",
    "SYNTHESE_FINALE_PHASE_2_100.md",
    "SYNTHESE_FINALE.md",
    "RESUME_JOUR.md",
    "RESUME_FINAL_COMPLET.md",
    "RESUME_1_PAGE.md"
)

foreach ($file in $finaux) {
    $stats.Total++
    if (Move-FileToArchive -FileName $file -ArchiveCategory "sessions") {
        $stats.Success++
    } else {
        $stats.NotFound++
    }
}

# ========================================
# CATÉGORIE 5: GUIDES ET PLANS (6)
# ========================================
Write-Host ""
Write-Status "📁 CATÉGORIE 5: Guides et Plans Obsolètes (6 fichiers)" "Info"

$guides = @(
    "ACTION_PLAN_IMMEDIAT.md",
    "DECISION_FINALE_PHASE2.md",
    "FINIR_PHASE2_STRATEGIE.md",
    "INDEX_DOCUMENTS.md",
    "TOUS_LES_DOCUMENTS.md",
    "DASHBOARD_SESSION_FINALE.md"
)

foreach ($file in $guides) {
    $stats.Total++
    if (Move-FileToArchive -FileName $file -ArchiveCategory "guides") {
        $stats.Success++
    } else {
        $stats.NotFound++
    }
}

# ========================================
# CATÉGORIE 6: MONITORING ANCIENS (8)
# ========================================
Write-Host ""
Write-Status "📁 CATÉGORIE 6: Analyses Monitoring Anciennes (8 fichiers)" "Info"

$monitoring = @(
    "ANALYSE_MONITORING_5MIN_2025-10-11.md",
    "RESUME_MONITORING_5MIN.md",
    "RAPPORT_MONITORING_V11.08_2025-10-12.md",
    "RESUME_MONITORING_V11.08.md",
    "monitor_15min_2025-10-13_11-26-56_analysis.md",
    "ANALYSE_COMPLETUDE_DONNEES_V11.md",
    "ANALYSE_ENVOI_DONNEES_SERVEUR.md",
    "RAPPORT_PROBLEME_ENVOI_POST.md"
)

foreach ($file in $monitoring) {
    $stats.Total++
    if (Move-FileToArchive -FileName $file -ArchiveCategory "monitoring") {
        $stats.Success++
    } else {
        $stats.NotFound++
    }
}

# ========================================
# CATÉGORIE 7: FIXES ET CORRECTIONS (15)
# ========================================
Write-Host ""
Write-Status "📁 CATÉGORIE 7: Corrections et Fixes de Bugs (15 fichiers)" "Info"

$fixes = @(
    "CORRECTIONS_V11.04.md",
    "VALIDATION_FIX_POST_V11.04.md",
    "CRASH_SLEEP_FIX_V11.06.md",
    "DIAGNOSTIC_COMPLET_CRASH_SLEEP_V11.06.md",
    "VERIFICATION_ENDPOINTS_V11.06.md",
    "FIX_POMPE_TANK_CYCLE_INFINI.md",
    "RESUME_FIX_POMPE_TANK_v4.5.19.md",
    "PROBLEME_INSERTION_BDD_SERVEUR.md",
    "CI_CD_FIX_2025-10-12.md",
    "CI_CD_STATUS.md",
    "RESUME_FIX_CI_CD.md",
    "RESUME_FIX_PANIC_MEMOIRE.md",
    "RESUME_CAPTURE_GURU_MEDITATION.md",
    "VALIDATION_V11.05_FINALE.md",
    "VALIDATION_V11.05_ETAT.md"
)

foreach ($file in $fixes) {
    $stats.Total++
    if (Move-FileToArchive -FileName $file -ArchiveCategory "fixes") {
        $stats.Success++
    } else {
        $stats.NotFound++
    }
}

# ========================================
# CATÉGORIE 8: AUDITS ET ANALYSES (6)
# ========================================
Write-Host ""
Write-Status "📁 CATÉGORIE 8: Audits et Analyses Techniques (6 fichiers)" "Info"

$analyses = @(
    "ANALYSE_EXHAUSTIVE_PROJET_ESP32_FFP5CS.md",
    "ANALYSE_APPROFONDIE_CACHES_OPTIMISATIONS.md",
    "DECOUVERTES_CRITIQUES_SUPPLEMENTAIRES.md",
    "AUDIT_CODE_PHASE2_v11.05.md",
    "AUDIT_SUMMARY_ACTIONS.md",
    "AUDIT_INDEX.md"
)

foreach ($file in $analyses) {
    $stats.Total++
    if (Move-FileToArchive -FileName $file -ArchiveCategory "analyses") {
        $stats.Success++
    } else {
        $stats.NotFound++
    }
}

# ========================================
# CATÉGORIE 9: VÉRIFICATIONS (5)
# ========================================
Write-Host ""
Write-Status "📁 CATÉGORIE 9: Guides de Vérification (5 fichiers)" "Info"

$verifications = @(
    "VERIFICATION_ENDPOINTS_ESP32.md",
    "VERIFICATION_FINALE_MIGRATION.md",
    "TEST_COHERENCE_FINALE.md",
    "RAPPORT_VERIFICATION_TOTALE.md",
    "CONFORMITE_HC_SR04.md"
)

foreach ($file in $verifications) {
    $stats.Total++
    if (Move-FileToArchive -FileName $file -ArchiveCategory "verification") {
        $stats.Success++
    } else {
        $stats.NotFound++
    }
}

# ========================================
# CATÉGORIE 10: GUIDES IMPLÉMENTATION (5)
# ========================================
Write-Host ""
Write-Status "📁 CATÉGORIE 10: Guides d'Implémentation Complétés (5 fichiers)" "Info"

$implementations = @(
    "README_PHASE2_IMPLEMENTATION.md",
    "PHASE_1_COMPLETE.md",
    "RAPPORT_IMPLEMENTATION_PHASES_2_3.md",
    "GUIDE_RAPIDE_PHASE2.md",
    "GUIDE_INTEGRATION_RATE_LIMITER.md"
)

foreach ($file in $implementations) {
    $stats.Total++
    if (Move-FileToArchive -FileName $file -ArchiveCategory "guides") {
        $stats.Success++
    } else {
        $stats.NotFound++
    }
}

# ========================================
# RÉSUMÉ FINAL
# ========================================
Write-Host ""
Write-Host "═══════════════════════════════════════════════════════════" -ForegroundColor Cyan
Write-Host "  📊 RÉSUMÉ DE L'ARCHIVAGE" -ForegroundColor Cyan
Write-Host "═══════════════════════════════════════════════════════════" -ForegroundColor Cyan
Write-Host ""

Write-Status "Fichiers traités:    $($stats.Total)" "Info"
Write-Status "Archivés avec succès: $($stats.Success)" "Success"
Write-Status "Introuvables:        $($stats.NotFound)" "Warning"
Write-Status "Erreurs:             $($stats.Failed)" $(if ($stats.Failed -gt 0) { "Error" } else { "Info" })

Write-Host ""

if ($DryRun) {
    Write-Status "⚠️  MODE SIMULATION - Aucune modification effectuée" "Warning"
    Write-Status "🔄 Pour exécuter réellement: .\archive_obsolete_docs.ps1" "Info"
} else {
    Write-Status "✅ Archivage terminé avec succès!" "Success"
    Write-Status "📁 Fichiers archivés dans: docs\archives\" "Info"
    Write-Host ""
    Write-Status "📝 Fichiers importants conservés à la racine:" "Info"
    Write-Host "   - README.md" -ForegroundColor Green
    Write-Host "   - START_HERE.md" -ForegroundColor Green
    Write-Host "   - VERSION.md" -ForegroundColor Green
    Write-Host "   - ETAT_FINAL_PROJET_V11.20.md" -ForegroundColor Green
    Write-Host "   - INDEX_ANALYSES_PROJET.md" -ForegroundColor Green
    Write-Host "   - INDEX_SESSION_2025-10-13.md" -ForegroundColor Green
}

Write-Host ""
Write-Host "═══════════════════════════════════════════════════════════" -ForegroundColor Cyan
Write-Host ""

