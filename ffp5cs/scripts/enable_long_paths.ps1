# Script PowerShell pour activer les chemins longs Windows
# Nécessite des privilèges administrateur
# Usage: Exécuter en tant qu'administrateur

Write-Host "🔧 Activation des chemins longs Windows..." -ForegroundColor Cyan

# Vérifier les privilèges administrateur
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "❌ ERREUR: Ce script doit être exécuté en tant qu'administrateur!" -ForegroundColor Red
    Write-Host "💡 Solution: Clic droit sur PowerShell → 'Exécuter en tant qu'administrateur'" -ForegroundColor Yellow
    exit 1
}

# Chemin de la clé de registre
$regPath = "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem"
$regName = "LongPathsEnabled"

# Vérifier si la clé existe déjà
$currentValue = Get-ItemProperty -Path $regPath -Name $regName -ErrorAction SilentlyContinue

if ($currentValue -and $currentValue.LongPathsEnabled -eq 1) {
    Write-Host "✅ Les chemins longs sont déjà activés!" -ForegroundColor Green
    Write-Host "   Valeur actuelle: $($currentValue.LongPathsEnabled)" -ForegroundColor Gray
} else {
    try {
        # Créer/modifier la valeur
        New-ItemProperty -Path $regPath -Name $regName -Value 1 -PropertyType DWORD -Force | Out-Null
        Write-Host "✅ Clé de registre créée/modifiée avec succès!" -ForegroundColor Green
        Write-Host "   LongPathsEnabled = 1" -ForegroundColor Gray
    } catch {
        Write-Host "❌ ERREUR lors de la modification du registre:" -ForegroundColor Red
        Write-Host "   $($_.Exception.Message)" -ForegroundColor Red
        exit 1
    }
}

Write-Host ""
Write-Host "⚠️  IMPORTANT: Un redémarrage de l'ordinateur est nécessaire pour que les changements prennent effet." -ForegroundColor Yellow
Write-Host ""
Write-Host "📝 Pour vérifier après redémarrage:" -ForegroundColor Cyan
Write-Host "   Get-ItemProperty -Path '$regPath' -Name '$regName'" -ForegroundColor Gray
Write-Host ""

# Demander si l'utilisateur veut redémarrer maintenant
$restart = Read-Host "Voulez-vous redémarrer maintenant? (O/N)"
if ($restart -eq "O" -or $restart -eq "o") {
    Write-Host "🔄 Redémarrage en cours..." -ForegroundColor Yellow
    Restart-Computer -Force
} else {
    Write-Host "ℹ️  N'oubliez pas de redémarrer votre ordinateur pour activer les chemins longs." -ForegroundColor Yellow
}
