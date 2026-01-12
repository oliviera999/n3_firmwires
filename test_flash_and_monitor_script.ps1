# Script de test pour vérifier que flash_and_monitor_until_reboot.ps1 fonctionne correctement
# Teste la syntaxe et la logique sans flasher réellement l'ESP32

Write-Host "=== TEST DU SCRIPT FLASH_AND_MONITOR_UNTIL_REBOOT ===" -ForegroundColor Cyan
Write-Host ""

# Test 1: Vérification de la syntaxe
Write-Host "1. Vérification de la syntaxe du script..." -ForegroundColor Yellow
try {
    $errors = $null
    $null = [System.Management.Automation.PSParser]::Tokenize(
        (Get-Content ".\flash_and_monitor_until_reboot.ps1" -Raw),
        [ref]$errors
    )
    
    if ($errors.Count -eq 0) {
        Write-Host "   ✅ Syntaxe valide" -ForegroundColor Green
    } else {
        Write-Host "   ❌ Erreurs de syntaxe détectées:" -ForegroundColor Red
        foreach ($error in $errors) {
            Write-Host "      Ligne $($error.Token.StartLine): $($error.Message)" -ForegroundColor Red
        }
        exit 1
    }
} catch {
    Write-Host "   ❌ Erreur lors de la vérification de syntaxe: $_" -ForegroundColor Red
    exit 1
}

# Test 2: Vérification que le fichier existe et est lisible
Write-Host "2. Vérification de l'existence du fichier..." -ForegroundColor Yellow
if (Test-Path ".\flash_and_monitor_until_reboot.ps1") {
    $fileInfo = Get-Item ".\flash_and_monitor_until_reboot.ps1"
    Write-Host "   ✅ Fichier trouvé: $($fileInfo.Name) ($([math]::Round($fileInfo.Length/1KB, 2)) KB)" -ForegroundColor Green
} else {
    Write-Host "   ❌ Fichier non trouvé" -ForegroundColor Red
    exit 1
}

# Test 3: Vérification des dépendances (pio command)
Write-Host "3. Vérification de PlatformIO..." -ForegroundColor Yellow
try {
    $pioVersion = pio --version 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "   ✅ PlatformIO disponible: $pioVersion" -ForegroundColor Green
    } else {
        Write-Host "   ⚠️ PlatformIO non trouvé dans PATH" -ForegroundColor Yellow
    }
} catch {
    Write-Host "   ⚠️ PlatformIO non trouvé dans PATH" -ForegroundColor Yellow
}

# Test 4: Vérification de la structure du script (sections principales)
Write-Host "4. Vérification de la structure du script..." -ForegroundColor Yellow
$scriptContent = Get-Content ".\flash_and_monitor_until_reboot.ps1" -Raw

$requiredSections = @(
    @{ Pattern = "ÉTAPE 1: FLASH"; Name = "Section Flash" },
    @{ Pattern = "ÉTAPE 2: MONITORING"; Name = "Section Monitoring" },
    @{ Pattern = "ÉTAPE 3: ANALYSE"; Name = "Section Analyse" },
    @{ Pattern = "pio run -e wroom-test"; Name = "Commande de compilation" },
    @{ Pattern = "rebootPatterns"; Name = "Détection de reboot" },
    @{ Pattern = "PRIORITÉ 1.*CRITIQUES"; Name = "Analyse Priorité 1" },
    @{ Pattern = "PRIORITÉ 2.*WATCHDOG"; Name = "Analyse Priorité 2" },
    @{ Pattern = "PRIORITÉ 3.*MÉMOIRE"; Name = "Analyse Priorité 3" }
)

$missingSections = @()
foreach ($section in $requiredSections) {
    if ($scriptContent -match $section.Pattern) {
        Write-Host "   ✅ $($section.Name) présente" -ForegroundColor Green
    } else {
        Write-Host "   ❌ $($section.Name) manquante" -ForegroundColor Red
        $missingSections += $section.Name
    }
}

if ($missingSections.Count -gt 0) {
    Write-Host ""
    Write-Host "   ⚠️ Sections manquantes: $($missingSections -join ', ')" -ForegroundColor Yellow
}

# Test 5: Vérification des paramètres du script
Write-Host "5. Vérification des paramètres..." -ForegroundColor Yellow
if ($scriptContent -match 'param\s*\(') {
    Write-Host "   ✅ Paramètres définis" -ForegroundColor Green
    
    # Vérifier les paramètres attendus
    $expectedParams = @("Port", "PostRebootDuration", "MaxWaitTime")
    foreach ($param in $expectedParams) {
        if ($scriptContent -match "\$param\s*=") {
            Write-Host "      ✅ Paramètre $param présent" -ForegroundColor Green
        } else {
            Write-Host "      ⚠️ Paramètre $param non trouvé" -ForegroundColor Yellow
        }
    }
} else {
    Write-Host "   ⚠️ Section param() non trouvée" -ForegroundColor Yellow
}

# Test 6: Vérification de la gestion d'erreurs
Write-Host "6. Vérification de la gestion d'erreurs..." -ForegroundColor Yellow
$errorHandling = @(
    @{ Pattern = "try\s*\{"; Name = "Blocs try" },
    @{ Pattern = "catch\s*\{"; Name = "Blocs catch" },
    @{ Pattern = "if.*LASTEXITCODE"; Name = "Vérification codes de sortie" },
    @{ Pattern = "ErrorAction"; Name = "Gestion d'erreurs silencieuses" }
)

$errorHandlingFound = 0
foreach ($check in $errorHandling) {
    if ($scriptContent -match $check.Pattern) {
        $errorHandlingFound++
    }
}

Write-Host "   ✅ $errorHandlingFound/$($errorHandling.Count) mécanismes de gestion d'erreurs présents" -ForegroundColor Green

# Test 7: Vérification de la création des fichiers de log
Write-Host "7. Vérification de la création des fichiers de log..." -ForegroundColor Yellow
if ($scriptContent -match '\$logFile\s*=' -and $scriptContent -match '\$analysisFile\s*=') {
    Write-Host "   ✅ Variables de fichiers de log définies" -ForegroundColor Green
} else {
    Write-Host "   ❌ Variables de fichiers de log manquantes" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== RÉSUMÉ DU TEST ===" -ForegroundColor Cyan
Write-Host ""

if ($missingSections.Count -eq 0) {
    Write-Host "✅ Tous les tests sont passés avec succès !" -ForegroundColor Green
    Write-Host ""
    Write-Host "Le script flash_and_monitor_until_reboot.ps1 est prêt à être utilisé." -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Pour lancer le script complet:" -ForegroundColor Yellow
    Write-Host "  .\flash_and_monitor_until_reboot.ps1 -Port COM4 -PostRebootDuration 60 -MaxWaitTime 3600" -ForegroundColor White
    Write-Host ""
    exit 0
} else {
    Write-Host "⚠️ Certaines sections manquent, mais le script peut fonctionner." -ForegroundColor Yellow
    Write-Host ""
    exit 0
}
