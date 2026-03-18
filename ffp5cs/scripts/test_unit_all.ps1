# Script PowerShell pour exécuter tous les tests unitaires
# Usage: .\scripts\test_unit_all.ps1

Write-Host "🧪 Exécution des tests unitaires du projet FFP5CS" -ForegroundColor Cyan
Write-Host ""

# Vérifier que PlatformIO est installé
$pioCmd = Get-Command pio -ErrorAction SilentlyContinue
if (-not $pioCmd) {
    Write-Host "❌ PlatformIO n'est pas installé ou n'est pas dans le PATH" -ForegroundColor Red
    Write-Host "   Installez PlatformIO: https://platformio.org/install/cli" -ForegroundColor Yellow
    exit 1
}

Write-Host "📦 Vérification de l'environnement..." -ForegroundColor Yellow

# Compiler les tests
Write-Host ""
Write-Host "🔨 Compilation des tests..." -ForegroundColor Yellow
pio test -c platformio-native.ini -e native --without-uploading

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "❌ Erreur de compilation" -ForegroundColor Red
    exit 1
}

# Exécuter les tests
Write-Host ""
Write-Host "▶️  Exécution des tests..." -ForegroundColor Yellow
Write-Host ""

$testOutput = pio test -c platformio-native.ini -e native -v 2>&1
$exitCode = $LASTEXITCODE

Write-Host $testOutput

Write-Host ""
Write-Host "========================================" -ForegroundColor Gray

if ($exitCode -eq 0) {
    Write-Host "✅ Tous les tests passent!" -ForegroundColor Green
    
    # Extraire les statistiques si disponibles
    $stats = $testOutput | Select-String -Pattern "(\d+) Tests (\d+) Failures (\d+) Ignored"
    if ($stats) {
        Write-Host ""
        Write-Host "📊 Statistiques:" -ForegroundColor Cyan
        Write-Host "   $stats" -ForegroundColor White
    }
    
    exit 0
} else {
    Write-Host "❌ Certains tests ont échoué" -ForegroundColor Red
    Write-Host ""
    Write-Host "💡 Pour plus de détails:" -ForegroundColor Yellow
    Write-Host "   pio test -e native -v" -ForegroundColor White
    exit 1
}
