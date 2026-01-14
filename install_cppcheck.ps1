# Script d'installation de cppcheck pour Windows
# Usage: .\install_cppcheck.ps1

Write-Host "🔍 Vérification de l'installation de cppcheck..." -ForegroundColor Cyan

# Vérifier si cppcheck est déjà installé
$cppcheckPath = Get-Command cppcheck -ErrorAction SilentlyContinue
if ($cppcheckPath) {
    Write-Host "✅ cppcheck est déjà installé : $($cppcheckPath.Source)" -ForegroundColor Green
    cppcheck --version
    exit 0
}

Write-Host "❌ cppcheck n'est pas installé." -ForegroundColor Yellow
Write-Host ""

# Vérifier si Chocolatey est installé
$chocoInstalled = Get-Command choco -ErrorAction SilentlyContinue

if ($chocoInstalled) {
    Write-Host "📦 Chocolatey détecté. Installation via Chocolatey..." -ForegroundColor Cyan
    Write-Host ""
    
    # Demander confirmation
    $response = Read-Host "Installer cppcheck via Chocolatey ? (O/N)"
    if ($response -eq "O" -or $response -eq "o") {
        Write-Host "Installation en cours..." -ForegroundColor Yellow
        choco install cppcheck -y
        
        if ($LASTEXITCODE -eq 0) {
            Write-Host "✅ Installation réussie !" -ForegroundColor Green
            cppcheck --version
            exit 0
        } else {
            Write-Host "❌ Erreur lors de l'installation via Chocolatey" -ForegroundColor Red
        }
    }
}

# Option manuelle
Write-Host ""
Write-Host "📥 Installation manuelle" -ForegroundColor Cyan
Write-Host ""
Write-Host "Pour installer cppcheck manuellement :" -ForegroundColor Yellow
Write-Host "1. Télécharger depuis : https://cppcheck.sourceforge.io/" -ForegroundColor White
Write-Host "2. Exécuter le fichier .msi téléchargé" -ForegroundColor White
Write-Host "3. Cocher 'Add to PATH' lors de l'installation" -ForegroundColor White
Write-Host ""
Write-Host "Ou installer Chocolatey puis relancer ce script :" -ForegroundColor Yellow
Write-Host "  Set-ExecutionPolicy Bypass -Scope Process -Force" -ForegroundColor White
Write-Host "  [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072" -ForegroundColor White
Write-Host "  iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))" -ForegroundColor White
Write-Host ""

# Vérifier si on peut utiliser winget (Windows 10/11)
$wingetInstalled = Get-Command winget -ErrorAction SilentlyContinue

if ($wingetInstalled) {
    Write-Host "📦 Windows Package Manager (winget) détecté." -ForegroundColor Cyan
    $response = Read-Host "Installer cppcheck via winget ? (O/N)"
    if ($response -eq "O" -or $response -eq "o") {
        Write-Host "Installation en cours..." -ForegroundColor Yellow
        winget install --id Cppcheck.Cppcheck -e --accept-package-agreements --accept-source-agreements
        
        if ($LASTEXITCODE -eq 0) {
            Write-Host "✅ Installation réussie !" -ForegroundColor Green
            # Rafraîchir le PATH
            $env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")
            cppcheck --version
            exit 0
        } else {
            Write-Host "❌ Erreur lors de l'installation via winget" -ForegroundColor Red
        }
    }
}

Write-Host ""
Write-Host "⚠️  Aucune méthode d'installation automatique disponible." -ForegroundColor Yellow
Write-Host "Veuillez installer cppcheck manuellement depuis https://cppcheck.sourceforge.io/" -ForegroundColor White
