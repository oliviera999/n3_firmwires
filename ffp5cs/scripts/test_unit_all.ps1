# Script PowerShell pour executer tous les tests unitaires
# Usage: .\scripts\test_unit_all.ps1

Write-Host "Execution des tests unitaires du projet FFP5CS" -ForegroundColor Cyan
Write-Host ""

# Verifier que PlatformIO est installe
$pioCmd = Get-Command pio -ErrorAction SilentlyContinue
if (-not $pioCmd) {
    Write-Host "PlatformIO n'est pas installe ou n'est pas dans le PATH" -ForegroundColor Red
    Write-Host "   Installez PlatformIO: https://platformio.org/install/cli" -ForegroundColor Yellow
    exit 1
}

Write-Host "Verification de l'environnement..." -ForegroundColor Yellow

# Executer chaque suite explicitement (plus fiable que l'execution agregee native).
$testSuites = @("test_config", "test_nvs", "test_server_url")
$allOutputs = @()
$exitCode = 0

Write-Host ""
Write-Host "Execution des tests..." -ForegroundColor Yellow
Write-Host ""

foreach ($suite in $testSuites) {
    Write-Host "-> Suite: $suite" -ForegroundColor Cyan
    $suiteOutput = pio test -c platformio-native.ini -e native -f $suite -v 2>&1
    $suiteExit = $LASTEXITCODE
    $allOutputs += $suiteOutput
    Write-Host $suiteOutput
    if ($suiteExit -ne 0) {
        $exitCode = $suiteExit
    }
    Write-Host ""
}

$testOutput = $allOutputs

Write-Host ""
Write-Host "========================================" -ForegroundColor Gray

if ($exitCode -eq 0) {
    Write-Host "Tous les tests passent." -ForegroundColor Green
    
    # Extraire les statistiques si disponibles
    $stats = $testOutput | Select-String -Pattern "(\d+) Tests (\d+) Failures (\d+) Ignored"
    if ($stats) {
        Write-Host ""
        Write-Host "Statistiques:" -ForegroundColor Cyan
        Write-Host "   $stats" -ForegroundColor White
    }
    
    exit 0
} else {
    Write-Host "Certains tests ont echoue" -ForegroundColor Red
    Write-Host ""
    Write-Host "Pour plus de details:" -ForegroundColor Yellow
    Write-Host "   pio test -e native -v" -ForegroundColor White
    exit 1
}
