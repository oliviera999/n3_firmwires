# Script de synchronisation du sous-module ffp3distant
# Usage: .\sync_ffp3distant.ps1 [update|push|pull]

param(
    [Parameter(Position=0)]
    [ValidateSet("update", "push", "pull", "status")]
    [string]$Action = "status"
)

Write-Host "🔄 Synchronisation du sous-module ffp3distant" -ForegroundColor Cyan

switch ($Action) {
    "update" {
        Write-Host "📥 Mise à jour du sous-module vers la dernière version..." -ForegroundColor Yellow
        git submodule update --remote ffp3distant
        
        if ($LASTEXITCODE -eq 0) {
            Write-Host "✅ Sous-module mis à jour avec succès" -ForegroundColor Green
            Write-Host "💾 Committing de la nouvelle référence..." -ForegroundColor Yellow
            git add ffp3distant
            git commit -m "Update ffp3distant submodule to latest version"
            Write-Host "✅ Référence mise à jour dans le projet principal" -ForegroundColor Green
        } else {
            Write-Host "❌ Erreur lors de la mise à jour du sous-module" -ForegroundColor Red
        }
    }
    
    "push" {
        Write-Host "📤 Poussage des modifications du sous-module..." -ForegroundColor Yellow
        cd ffp3distant
        
        # Vérifier s'il y a des modifications
        $status = git status --porcelain
        if ($status) {
            Write-Host "📝 Modifications détectées dans ffp3distant" -ForegroundColor Yellow
            git add .
            git commit -m "Update ffp3distant content"
            git push origin main
            
            if ($LASTEXITCODE -eq 0) {
                Write-Host "✅ Modifications poussées vers le dépôt distant" -ForegroundColor Green
            } else {
                Write-Host "❌ Erreur lors du push" -ForegroundColor Red
                cd ..
                exit 1
            }
        } else {
            Write-Host "ℹ️ Aucune modification détectée dans ffp3distant" -ForegroundColor Blue
        }
        
        cd ..
        
        # Mettre à jour la référence dans le projet principal
        Write-Host "🔄 Mise à jour de la référence dans le projet principal..." -ForegroundColor Yellow
        git add ffp3distant
        git commit -m "Update ffp3distant submodule reference"
        Write-Host "✅ Référence mise à jour dans le projet principal" -ForegroundColor Green
    }
    
    "pull" {
        Write-Host "📥 Récupération des dernières modifications..." -ForegroundColor Yellow
        git submodule update --remote --merge ffp3distant
        
        if ($LASTEXITCODE -eq 0) {
            Write-Host "✅ Sous-module mis à jour avec les dernières modifications" -ForegroundColor Green
        } else {
            Write-Host "❌ Erreur lors de la récupération" -ForegroundColor Red
        }
    }
    
    "status" {
        Write-Host "📊 État du sous-module ffp3distant:" -ForegroundColor Cyan
        git submodule status ffp3distant
        
        Write-Host "`n📋 État du projet principal:" -ForegroundColor Cyan
        git status --short
    }
}

Write-Host "`n🎉 Synchronisation terminée!" -ForegroundColor Green
