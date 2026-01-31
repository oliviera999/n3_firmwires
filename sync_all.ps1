# Script de synchronisation complète du projet avec sous-modules
# Usage: .\sync_all.ps1

Write-Host "🚀 Synchronisation complète du projet ffp5cs" -ForegroundColor Cyan

# Vérifier l'état initial
Write-Host "`n📊 État initial:" -ForegroundColor Yellow
git status --short

# Mettre à jour tous les sous-modules
Write-Host "`n🔄 Mise à jour des sous-modules..." -ForegroundColor Yellow
git submodule update --remote --merge

if ($LASTEXITCODE -eq 0) {
    Write-Host "✅ Sous-modules mis à jour" -ForegroundColor Green
} else {
    Write-Host "❌ Erreur lors de la mise à jour des sous-modules" -ForegroundColor Red
    exit 1
}

# Vérifier s'il y a des changements à commiter
$changes = git status --porcelain
if ($changes) {
    Write-Host "`n📝 Changements détectés:" -ForegroundColor Yellow
    git status --short
    
    Write-Host "`n💾 Commit des changements..." -ForegroundColor Yellow
    git add .
    git commit -m "Update project and submodules"
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✅ Changements commités" -ForegroundColor Green
    } else {
        Write-Host "❌ Erreur lors du commit" -ForegroundColor Red
    }
} else {
    Write-Host "`nℹ️ Aucun changement détecté" -ForegroundColor Blue
}

Write-Host "`n🎉 Synchronisation complète terminée!" -ForegroundColor Green
