# Script de vérification des mails depuis le dernier log
param(
    [string]$LogFile = ""
)

if ([string]::IsNullOrEmpty($LogFile)) {
    # Trouver le dernier fichier de log : d'abord dans logs/, puis racine
    $logsDir = Join-Path $PSScriptRoot "logs"
    $candidates = @()
    if (Test-Path $logsDir) {
        $candidates = @(Get-ChildItem -Path $logsDir -Filter "monitor_5min_*.log" -ErrorAction SilentlyContinue)
        $candidates += @(Get-ChildItem -Path $logsDir -Filter "monitor_*min_*.log" -ErrorAction SilentlyContinue)
        $candidates += @(Get-ChildItem -Path $logsDir -Filter "monitor_wroom_test_*.log" -ErrorAction SilentlyContinue)
    }
    if ($candidates.Count -eq 0) {
        $candidates = @(Get-ChildItem -Path $PSScriptRoot -Filter "monitor_5min_*.log" -ErrorAction SilentlyContinue)
        $candidates += @(Get-ChildItem -Path $PSScriptRoot -Filter "monitor_wroom_test_*.log" -ErrorAction SilentlyContinue)
    }
    $latest = $candidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $LogFile = if ($latest) { $latest.FullName } else { $null }

    if (-not $LogFile) {
        Write-Host "❌ Aucun fichier de log trouvé" -ForegroundColor Red
        exit 1
    }
}

Write-Host "=== VÉRIFICATION DES MAILS DEPUIS LE LOG ===" -ForegroundColor Green
Write-Host "Fichier: $LogFile" -ForegroundColor Cyan
Write-Host ""

if (-not (Test-Path $LogFile)) {
    Write-Host "❌ Fichier non trouvé: $LogFile" -ForegroundColor Red
    exit 1
}

$lines = Get-Content $LogFile

# Structures de données pour stocker les informations
$expectedEmails = @()  # Événements qui devraient déclencher un mail
$queuedEmails = @()    # Mails ajoutés à la queue
$processedEmails = @() # Mails traités avec succès
$failedEmails = @()    # Mails échoués
$queueFullErrors = @() # Erreurs de queue pleine

# Fonction pour extraire un timestamp approximatif depuis la ligne
function Get-LineTimestamp {
    param([string]$line)
    
    # Chercher un pattern de timestamp (format variable selon les logs)
    if ($line -match '(\d{4}-\d{2}-\d{2}[\s:]\d{2}:\d{2}:\d{2})') {
        return $matches[1]
    }
    # Sinon, utiliser le numéro de ligne comme référence
    return $null
}

# Fonction pour normaliser un sujet de mail (pour la corrélation)
function Normalize-Subject {
    param([string]$subject)
    
    if ([string]::IsNullOrEmpty($subject)) {
        return ""
    }
    
    # Retirer les guillemets et espaces
    $normalized = $subject.Trim().Trim("'").Trim('"')
    
    # Retirer les préfixes communs pour faciliter la corrélation
    $normalized = $normalized -replace '^FFP5CS\s*-\s*', ''
    $normalized = $normalized -replace '^FFP3\s*-\s*', ''
    
    return $normalized
}

# Fonction pour vérifier si deux sujets correspondent (corrélation flexible)
function Test-SubjectMatch {
    param([string]$subject1, [string]$subject2)
    
    $norm1 = Normalize-Subject $subject1
    $norm2 = Normalize-Subject $subject2
    
    if ([string]::IsNullOrEmpty($norm1) -or [string]::IsNullOrEmpty($norm2)) {
        return $false
    }
    
    # Correspondance exacte
    if ($norm1 -eq $norm2) {
        return $true
    }
    
    # Correspondance partielle (l'un contient l'autre)
    # v11.175: Utiliser .Contains() au lieu de -like pour éviter WildcardPatternException
    # (les crochets [] dans les sujets comme "Réserve OK" causaient des erreurs)
    if ($norm1.Contains($norm2) -or $norm2.Contains($norm1)) {
        return $true
    }
    
    # Correspondance pour les sujets avec version (ex: "Démarrage système v11.156" vs "Démarrage système v11.157")
    if ($norm1 -match '^(.+?)\s+v\d+\.\d+') {
        $base1 = $matches[1]
        if ($norm2 -match '^(.+?)\s+v\d+\.\d+') {
            $base2 = $matches[1]
            if ($base1 -eq $base2) {
                return $true
            }
        }
        # L'un a la version, l'autre est juste le base (ex: "Démarrage système" attendu vs "Démarrage système v12.07" envoyé)
        if ($norm2 -eq $base1) {
            return $true
        }
    }
    if ($norm2 -match '^(.+?)\s+v\d+\.\d+') {
        $base2 = $matches[1]
        if ($norm1 -eq $base2) {
            return $true
        }
    }
    
    return $false
}

# 1. PARSING DES ÉVÉNEMENTS ATTENDUS

Write-Host "🔍 Analyse des événements déclencheurs..." -ForegroundColor Yellow

# 1.1 Mail de démarrage (Boot) — une seule entrée attendue par boot (Alerte ajoutée = mise en queue)
$bootPattern = '\[Mail\].*Alerte ajoutée.*queue.*Démarrage'
$bootMatches = $lines | Select-String -Pattern $bootPattern
foreach ($match in $bootMatches) {
    $expectedEmails += @{
        Type = "Boot"
        LineNumber = $match.LineNumber
        Line = $match.Line.Trim()
        ExpectedSubject = "Démarrage système"
        Timestamp = Get-LineTimestamp $match.Line
    }
}

# 1.2 Nourrissage automatique
$feedingPattern = '\[FeedingSchedule\].*(Bouffe matin|Bouffe midi|Bouffe soir)'
$feedingMatches = $lines | Select-String -Pattern $feedingPattern
foreach ($match in $feedingMatches) {
    $type = ""
    if ($match.Line -match 'Bouffe matin') { $type = "Bouffe matin" }
    elseif ($match.Line -match 'Bouffe midi') { $type = "Bouffe midi" }
    elseif ($match.Line -match 'Bouffe soir') { $type = "Bouffe soir" }
    
    $expectedEmails += @{
        Type = "Nourrissage"
        SubType = $type
        LineNumber = $match.LineNumber
        Line = $match.Line.Trim()
        ExpectedSubject = "FFP5CS - $type"
        Timestamp = Get-LineTimestamp $match.Line
    }
}

# 1.3 Nourrissage manuel
$manualFeedingPattern = '\[Web\].*?=== ENVOI EMAIL|bouffePetits|bouffeGros'
$manualMatches = $lines | Select-String -Pattern $manualFeedingPattern
foreach ($match in $manualMatches) {
    $type = "Nourrissage manuel"
    if ($match.Line -match 'bouffePetits') { $type = "Nourrissage manuel (petits)" }
    elseif ($match.Line -match 'bouffeGros') { $type = "Nourrissage manuel (gros)" }
    
    $expectedEmails += @{
        Type = "Nourrissage"
        SubType = $type
        LineNumber = $match.LineNumber
        Line = $match.Line.Trim()
        ExpectedSubject = $type
        Timestamp = Get-LineTimestamp $match.Line
    }
}

# 1.4 Alertes niveaux d'eau et actionneurs (état physique)
# Chauffage ON/OFF et Lumière ON/OFF : NE PAS compter comme attendus ici. Le firmware logue "Chauffage OFF"
# à chaque application de config serveur (GPIOParser), mais n'envoie un mail que sur transition d'état
# (thermostat/automatism). On ne compte donc que les alertes qui déclenchent vraiment un envoi.
$alertPatterns = @{
    "Alerte - Niveau aquarium BAS" = 'Alerte.*Niveau aquarium.*BAS|\[Auto\].*Niveau aquarium.*BAS'
    "Alerte - Aquarium TROP PLEIN" = 'Alerte.*Aquarium.*TROP PLEIN|\[Auto\].*TROP PLEIN|Email TROP PLEIN'
    "Alerte - Réserve BASSE" = 'Alerte.*Réserve.*BASSE|\[Auto\].*Réserve.*BASSE'
    "Info - Réserve OK" = 'Info.*Réserve.*OK|\[Auto\].*Réserve.*OK'
    # Chauffage/Lumière ON/OFF : retirés — le log "Chauffage OFF" vient du GPIOParser à chaque GET,
    # pas d'un envoi mail (mail uniquement sur transition dans checkAlertsAndSendEmails).
}

foreach ($alertType in $alertPatterns.Keys) {
    $pattern = $alertPatterns[$alertType]
    $matches = $lines | Select-String -Pattern $pattern
    foreach ($match in $matches) {
        # Éviter les doublons (ex: "Email TROP PLEIN envoyé" ne doit pas être compté comme un événement déclencheur)
        if ($match.Line -match 'Email.*envoyé|Échec envoi') {
            continue
        }
        
        $expectedEmails += @{
            Type = "Alerte"
            SubType = $alertType
            LineNumber = $match.LineNumber
            Line = $match.Line.Trim()
            ExpectedSubject = $alertType
            Timestamp = Get-LineTimestamp $match.Line
        }
    }
}

# 1.5 Sleep/Wake
$sleepPattern = '\[Mail\]\s*=====\s*ENVOI MAIL VEILLE\s*====='
$wakePattern = '\[Mail\]\s*=====\s*ENVOI MAIL RÉVEIL\s*====='
$sleepMatches = $lines | Select-String -Pattern $sleepPattern
$wakeMatches = $lines | Select-String -Pattern $wakePattern

foreach ($match in $sleepMatches) {
    $expectedEmails += @{
        Type = "Sleep/Wake"
        SubType = "Sleep"
        LineNumber = $match.LineNumber
        Line = $match.Line.Trim()
        ExpectedSubject = "ENVOI MAIL VEILLE"
        Timestamp = Get-LineTimestamp $match.Line
    }
}

foreach ($match in $wakeMatches) {
    $expectedEmails += @{
        Type = "Sleep/Wake"
        SubType = "Wake"
        LineNumber = $match.LineNumber
        Line = $match.Line.Trim()
        ExpectedSubject = "ENVOI MAIL RÉVEIL"
        Timestamp = Get-LineTimestamp $match.Line
    }
}

# 1.6 OTA Updates — uniquement les lignes qui indiquent un envoi email OTA (début/fin ou envoi explicite)
$otaPattern = 'OTA début|OTA fin|Envoi email.*mise a jour OTA|Envoi email.*OTA|\[App\].*Envoi email.*OTA'
$otaMatches = $lines | Select-String -Pattern $otaPattern
foreach ($match in $otaMatches) {
    # Éviter les doublons
    if ($match.Line -match 'Email.*envoyé|Email.*échoué') {
        continue
    }
    
    $type = "OTA"
    if ($match.Line -match 'début') { $type = "OTA début" }
    elseif ($match.Line -match 'fin') { $type = "OTA fin" }
    
    $expectedEmails += @{
        Type = "OTA"
        SubType = $type
        LineNumber = $match.LineNumber
        Line = $match.Line.Trim()
        ExpectedSubject = $type
        Timestamp = Get-LineTimestamp $match.Line
    }
}

# 1.7 Remplissage (Refill)
$refillPattern = '\[Auto\].*Email.*remplissage|\[CRITIQUE\].*remplissage'
$refillMatches = $lines | Select-String -Pattern $refillPattern
foreach ($match in $refillMatches) {
    # Ne compter que les événements déclencheurs, pas les confirmations
    if ($match.Line -match 'Email.*envoyé|Email.*échoué') {
        continue
    }
    
    $type = "Remplissage"
    if ($match.Line -match 'démarrage') { $type = "Remplissage - Démarrage" }
    elseif ($match.Line -match 'fin|terminé') { $type = "Remplissage - Fin" }
    
    $expectedEmails += @{
        Type = "Remplissage"
        SubType = $type
        LineNumber = $match.LineNumber
        Line = $match.Line.Trim()
        ExpectedSubject = $type
        Timestamp = Get-LineTimestamp $match.Line
    }
}

Write-Host "  ✅ $($expectedEmails.Count) événements déclencheurs identifiés" -ForegroundColor Green
Write-Host ""

# 2. PARSING DES TRACES D'ENVOI

Write-Host "🔍 Analyse des traces d'envoi..." -ForegroundColor Yellow

# 2.1 Mails ajoutés à la queue
# Note: Pattern flexible qui ignore les emojis (cherche "Alerte ajoutée" ou "Mail ajouté")
$queuedPattern = '\[Mail\].*?(?:Alerte ajoutée|Mail ajouté).*?queue.*?\(.*?en attente\):.*?[''"](.+?)[''"]|\[Mail\].*?(?:Alerte ajoutée|Mail ajouté).*?queue.*?\(.*?en attente\):.*?"(.+?)"'
$queuedMatches = $lines | Select-String -Pattern $queuedPattern
foreach ($match in $queuedMatches) {
    $subject = ""
    # Essayer d'extraire le sujet entre guillemets simples ou doubles
    if ($match.Line -match "[''""](.+?)[''""]") {
        $subject = $matches[1]
    }
    # Si pas de guillemets, essayer d'extraire après le deux-points
    elseif ($match.Line -match 'queue.*?\(.*?en attente\):\s*(.+?)(?:\s*$|\s*\[)') {
        $subject = $matches[1].Trim().Trim("'").Trim('"')
    }
    
    if (-not [string]::IsNullOrEmpty($subject)) {
        $queuedEmails += @{
            Subject = $subject
            LineNumber = $match.LineNumber
            Line = $match.Line.Trim()
            Timestamp = Get-LineTimestamp $match.Line
        }
    }
}

# 2.2 Mails traités avec succès (asynchrones via queue)
# Note: Patterns flexibles pour gérer les emojis
# Succès réel = SMTP (Traitement mail séquentiel + Mail/Message SMTP envoyé)
$processedPattern = '\[Mail\].*?Traitement mail séquentiel:.*?[''"](.+?)[''"]|\[Mail\].*?Traitement mail séquentiel:.*?"(.+?)"|\[Mail\].*?Mail SMTP envoyé avec succès|\[Mail\].*?Message SMTP envoyé avec succès'
$processedMatches = $lines | Select-String -Pattern $processedPattern
foreach ($match in $processedMatches) {
    $subject = ""
    if ($match.Line -match "''(.+?)''") {
        $subject = $matches[1]
    }
    elseif ($match.Line -match '"(.+?)"') {
        $subject = $matches[1]
    }
    elseif ($match.Line -match 'Traitement mail séquentiel:') {
        # Extraire le sujet même s'il n'est pas entre guillemets
        if ($match.Line -match "Traitement mail séquentiel:\s*(.+?)(?:\s*$|\s*\[)") {
            $subject = $matches[1].Trim().Trim("'").Trim('"')
        }
    }
    
    # Pour les lignes de succès sans sujet, on va chercher le mail traité juste avant
    if ([string]::IsNullOrEmpty($subject) -and $match.Line -match 'SMTP envoyé avec succès') {
        # Chercher le mail traité précédemment dans les lignes
        $searchStart = [Math]::Max(1, $match.LineNumber - 10)
        $searchEnd = $match.LineNumber
        for ($i = $searchStart; $i -le $searchEnd; $i++) {
            if ($i -lt $lines.Count) {
                $prevLine = $lines[$i - 1]
                if ($prevLine -match "Traitement mail séquentiel:.*''(.+?)''") {
                    $subject = $matches[1]
                    break
                }
                elseif ($prevLine -match 'Traitement mail séquentiel:\s*(.+?)(?:\s*$|\s*\[)') {
                    $subject = $matches[1].Trim().Trim("'").Trim('"')
                    break
                }
            }
        }
    }
    
    if (-not [string]::IsNullOrEmpty($subject)) {
        $processedEmails += @{
            Subject = $subject
            LineNumber = $match.LineNumber
            Line = $match.Line.Trim()
            Timestamp = Get-LineTimestamp $match.Line
            IsSync = $false
        }
    }
}

# 2.2b Mails synchrones (envoyés directement sans queue - rare)
# Avec le firmware actuel, send/sendAlert sont asynchrones (queue). "Ajouté à la queue" n'est pas un envoi SMTP.
# Les vrais envois sont dans processedPattern (Traitement mail séquentiel + Mail SMTP envoyé avec succès).
# Ce pattern ne matche plus les nouveaux logs (on ne log plus "ENVOYÉ"/"envoyé" pour les ajouts queue).
$syncPattern = '\[FeedingSchedule\].*?Email de nourrissage envoyé:\s*(.+)|\[Auto\].*?Email.*?envoyé|\[Web\].*?Email.*?envoyé|Mail de démarrage ENVOYÉ|\[App\].*?Email.*?envoyé'
$syncMatches = $lines | Select-String -Pattern $syncPattern
foreach ($match in $syncMatches) {
    $subject = ""
    if ($match.Line -match 'Email de nourrissage envoyé:\s*(.+)') {
        $subject = "FFP5CS - $($matches[1].Trim())"
    }
    elseif ($match.Line -match 'Mail de démarrage') {
        $subject = "Démarrage système"
    }
    elseif ($match.Line -match 'Email.*envoyé') {
        # Essayer d'extraire le type depuis le contexte
        if ($match.Line -match 'remplissage') {
            if ($match.Line -match 'démarrage') {
                $subject = "Remplissage - Démarrage"
            } else {
                $subject = "Remplissage - Fin"
            }
        }
        elseif ($match.Line -match 'TROP PLEIN') {
            $subject = "Alerte - Aquarium TROP PLEIN"
        }
        else {
            # Sujet générique pour les mails synchrones non identifiés
            $subject = "Mail synchrone"
        }
    }
    
    if (-not [string]::IsNullOrEmpty($subject)) {
        $processedEmails += @{
            Subject = $subject
            LineNumber = $match.LineNumber
            Line = $match.Line.Trim()
            Timestamp = Get-LineTimestamp $match.Line
            IsSync = $true
        }
    }
}

# 2.3 Mails échoués
$failedPattern = '\[Mail\].*?Échec envoi mail|\[Mail\].*?ERREUR sendMail|\[FeedingSchedule\].*?Échec envoi email|\[FeedingSchedule\].*?Échec ajout à la queue|Échec ajout à la queue mail de démarrage'
$failedMatches = $lines | Select-String -Pattern $failedPattern
foreach ($match in $failedMatches) {
    $subject = ""
    # Essayer d'extraire le sujet depuis la ligne ou les lignes précédentes
    if ($match.Line -match "Échec envoi email:\s*(.+)") {
        $subject = $matches[1].Trim()
    }
    elseif ($match.Line -match "Échec envoi mail") {
        # Chercher le mail traité précédemment
        $searchStart = [Math]::Max(1, $match.LineNumber - 10)
        for ($i = $searchStart; $i -lt $match.LineNumber; $i++) {
            if ($i -lt $lines.Count) {
                $prevLine = $lines[$i - 1]
                if ($prevLine -match "Traitement mail séquentiel:.*''(.+?)''") {
                    $subject = $matches[1]
                    break
                }
            }
        }
    }
    
    $failedEmails += @{
        Subject = $subject
        LineNumber = $match.LineNumber
        Line = $match.Line.Trim()
        Timestamp = Get-LineTimestamp $match.Line
        Error = $match.Line.Trim()
    }
}

# 2.4 Erreurs de queue pleine
$queueFullPattern = '\[Mail\].*?Queue pleine'
$queueFullMatches = $lines | Select-String -Pattern $queueFullPattern
foreach ($match in $queueFullMatches) {
    $queueFullErrors += @{
        LineNumber = $match.LineNumber
        Line = $match.Line.Trim()
        Timestamp = Get-LineTimestamp $match.Line
    }
}

Write-Host "  ✅ $($queuedEmails.Count) mails ajoutés à la queue" -ForegroundColor Green
Write-Host "  ✅ $($processedEmails.Count) mails traités" -ForegroundColor Green
Write-Host "  ⚠️ $($failedEmails.Count) mails échoués" -ForegroundColor Yellow
Write-Host "  ⚠️ $($queueFullErrors.Count) erreurs de queue pleine" -ForegroundColor Yellow
Write-Host ""

# 3. CORRÉLATION ET ANALYSE

Write-Host "🔍 Corrélation des événements avec les mails..." -ForegroundColor Yellow

$emailStatus = @()
$unmatchedQueued = @($queuedEmails)
$unmatchedProcessed = @($processedEmails)

foreach ($expected in $expectedEmails) {
    $status = @{
        Expected = $expected
        Queued = $null
        Processed = $null
        Failed = $null
        Status = "Manquant"
    }
    
    # Chercher un mail en queue correspondant
    foreach ($queued in $unmatchedQueued) {
        if (Test-SubjectMatch $expected.ExpectedSubject $queued.Subject) {
            $status.Queued = $queued
            $status.Status = "En queue"
            $unmatchedQueued = $unmatchedQueued | Where-Object { $_ -ne $queued }
            break
        }
    }
    
    # Chercher un mail traité correspondant
    foreach ($processed in $unmatchedProcessed) {
        if (Test-SubjectMatch $expected.ExpectedSubject $processed.Subject) {
            $status.Processed = $processed
            $status.Status = "Envoyé"
            $unmatchedProcessed = $unmatchedProcessed | Where-Object { $_ -ne $processed }
            break
        }
    }
    
    # Chercher un mail échoué correspondant
    foreach ($failed in $failedEmails) {
        if (Test-SubjectMatch $expected.ExpectedSubject $failed.Subject) {
            $status.Failed = $failed
            $status.Status = "Échoué"
            break
        }
    }
    
    # Si en queue mais pas traité, marquer comme non traité
    if ($status.Queued -and -not $status.Processed -and -not $status.Failed) {
        $status.Status = "Non traité"
    }
    
    $emailStatus += $status
}

# Mails en queue non corrélés (peuvent être des mails non attendus ou des doublons)
$orphanQueued = $unmatchedQueued
$orphanProcessed = $unmatchedProcessed

# 4. GÉNÉRATION DU RAPPORT

Write-Host ""
Write-Host '=== RAPPORT D''ANALYSE ===' -ForegroundColor Green
Write-Host ""

# 4.1 Résumé
$totalExpected = $expectedEmails.Count
$totalQueued = $queuedEmails.Count
$totalProcessed = $processedEmails.Count
$totalFailed = $failedEmails.Count
$totalNotProcessed = ($emailStatus | Where-Object { $_.Status -eq "Non traité" }).Count
$totalSent = ($emailStatus | Where-Object { $_.Status -eq "Envoyé" }).Count
$totalMissing = ($emailStatus | Where-Object { $_.Status -eq "Manquant" }).Count

Write-Host "📊 RÉSUMÉ" -ForegroundColor Yellow
Write-Host "  Mails attendus: $totalExpected" -ForegroundColor White
Write-Host "  Mails ajoutés à la queue: $totalQueued" -ForegroundColor White
Write-Host "  Mails envoyés avec succès: $totalSent" -ForegroundColor $(if ($totalSent -eq $totalExpected) { "Green" } else { "Yellow" })
Write-Host "  Mails échoués: $totalFailed" -ForegroundColor $(if ($totalFailed -eq 0) { "Green" } else { "Red" })
Write-Host "  Mails non traités: $totalNotProcessed" -ForegroundColor $(if ($totalNotProcessed -eq 0) { "Green" } else { "Yellow" })
Write-Host "  Mails manquants: $totalMissing" -ForegroundColor $(if ($totalMissing -eq 0) { "Green" } else { "Red" })
Write-Host ""

# 4.2 Détail par type
Write-Host "📧 DÉTAIL PAR TYPE" -ForegroundColor Yellow

$types = $emailStatus | Group-Object { $_.Expected.Type }
foreach ($typeGroup in $types) {
    $typeName = $typeGroup.Name
    $typeEmails = $typeGroup.Group
    $typeExpected = $typeEmails.Count
    $typeSent = ($typeEmails | Where-Object { $_.Status -eq "Envoyé" }).Count
    $typeFailed = ($typeEmails | Where-Object { $_.Status -eq "Échoué" }).Count
    $typeNotProcessed = ($typeEmails | Where-Object { $_.Status -eq "Non traité" }).Count
    $typeMissing = ($typeEmails | Where-Object { $_.Status -eq "Manquant" }).Count
    
    $statusIcon = "✅"
    if ($typeFailed -gt 0 -or $typeNotProcessed -gt 0 -or $typeMissing -gt 0) {
        $statusIcon = "⚠️"
    }
    
    $details = "${typeName}: $typeExpected attendu(s)"
    if ($typeSent -gt 0) { $details += ", $typeSent envoyé(s)" }
    if ($typeFailed -gt 0) { $details += ", $typeFailed échoué(s)" }
    if ($typeNotProcessed -gt 0) { $details += ", $typeNotProcessed non traité(s)" }
    if ($typeMissing -gt 0) { $details += ", $typeMissing manquant(s)" }
    
    Write-Host "  $details $statusIcon" -ForegroundColor White
}
Write-Host ""

# 4.3 Mails manquants
$missingEmails = $emailStatus | Where-Object { $_.Status -eq "Manquant" }
if ($missingEmails.Count -gt 0) {
    Write-Host "❌ MAILS MANQUANTS ($($missingEmails.Count))" -ForegroundColor Red
    foreach ($missing in $missingEmails) {
        $timestamp = if ($missing.Expected.Timestamp) { '[' + $missing.Expected.Timestamp + ']' } else { '[Ligne ' + $missing.Expected.LineNumber + ']' }
        $subject = $missing.Expected.ExpectedSubject
        if ($missing.Expected.SubType) {
            $subject = "$subject ($($missing.Expected.SubType))"
        }
        Write-Host "  - $timestamp $subject" -ForegroundColor Yellow
    }
    Write-Host ""
}

# 4.4 Mails non traités
$notProcessedEmails = $emailStatus | Where-Object { $_.Status -eq "Non traité" }
if ($notProcessedEmails.Count -gt 0) {
    Write-Host "⚠️ MAILS NON TRAITÉS ($($notProcessedEmails.Count))" -ForegroundColor Yellow
    foreach ($notProcessed in $notProcessedEmails) {
        $timestamp = if ($notProcessed.Queued.Timestamp) { '[' + $notProcessed.Queued.Timestamp + ']' } else { '[Ligne ' + $notProcessed.Queued.LineNumber + ']' }
        $subject = $notProcessed.Queued.Subject
        Write-Host ('  - ' + $timestamp + ' ' + $subject + ' (ajoute a la queue mais jamais traite)') -ForegroundColor Yellow
    }
    Write-Host ""
}

# 4.5 Mails echoues
if ($failedEmails.Count -gt 0) {
    $nFailed = $failedEmails.Count
    Write-Host ('[KO] MAILS ECHOUES (' + $nFailed + ')') -ForegroundColor Red
    foreach ($failed in $failedEmails) {
        $timestamp = if ($failed.Timestamp) { '[' + $failed.Timestamp + ']' } else { '[Ligne ' + $failed.LineNumber + ']' }
        $subject = if ($failed.Subject) { $failed.Subject } else { 'Sujet inconnu' }
        Write-Host ('  - ' + $timestamp + ' ' + $subject) -ForegroundColor Yellow
        Write-Host ('    Erreur: ' + $failed.Error) -ForegroundColor Gray
    }
    Write-Host ''
}

# 4.6 Erreurs de queue pleine
if ($queueFullErrors.Count -gt 0) {
    $nQ = $queueFullErrors.Count
    Write-Host ('[WARN] ERREURS DE QUEUE PLEINE (' + $nQ + ')') -ForegroundColor Yellow
    foreach ($error in $queueFullErrors) {
        $timestamp = if ($error.Timestamp) { '[' + $error.Timestamp + ']' } else { '[Ligne ' + $error.LineNumber + ']' }
        Write-Host ('  - ' + $timestamp + ' ' + $error.Line) -ForegroundColor Yellow
    }
    Write-Host ''
}

# 4.7 Mails envoyes avec succes (resume)
$sentEmails = $emailStatus | Where-Object { $_.Status -match 'Envoy' }
if ($sentEmails.Count -gt 0 -and $sentEmails.Count -le 20) {
    $nSent = $sentEmails.Count
    Write-Host ('[OK] MAILS ENVOYES AVEC SUCCES (' + $nSent + ')') -ForegroundColor Green
    foreach ($sent in $sentEmails) {
        $timestamp = if ($sent.Processed.Timestamp) { '[' + $sent.Processed.Timestamp + ']' } else { '[Ligne ' + $sent.Processed.LineNumber + ']' }
        $subject = $sent.Processed.Subject
        Write-Host ('  - ' + $timestamp + ' ' + $subject) -ForegroundColor White
    }
    Write-Host ''
} elseif ($sentEmails.Count -gt 20) {
    $nSent = $sentEmails.Count
    Write-Host ('[OK] MAILS ENVOYES AVEC SUCCES (' + $nSent + ')') -ForegroundColor Green
    Write-Host '  (Trop nombreux pour afficher la liste complete)' -ForegroundColor Gray
    Write-Host ''
}

# 4.8 Mails orphelins (en queue mais non attendus)
if ($orphanQueued.Count -gt 0) {
    $nOrphan = $orphanQueued.Count
    Write-Host ('[INFO] MAILS EN QUEUE NON ATTENDUS (' + $nOrphan + ')') -ForegroundColor Cyan
    foreach ($orphan in $orphanQueued) {
        $timestamp = if ($orphan.Timestamp) { '[' + $orphan.Timestamp + ']' } else { '[Ligne ' + $orphan.LineNumber + ']' }
        Write-Host ('  - ' + $timestamp + ' ' + $orphan.Subject) -ForegroundColor Gray
    }
    Write-Host ''
}

Write-Host '=== ANALYSE TERMINEE ===' -ForegroundColor Green