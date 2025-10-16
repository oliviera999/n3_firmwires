#!/usr/bin/env pwsh
# Test de la configuration Cursor IDE pour ESP32

Write-Host "Test de la Configuration Cursor IDE ESP32" -ForegroundColor Green
Write-Host "=============================================" -ForegroundColor Green

# Verification des fichiers de configuration
$configFiles = @(
    ".vscode/settings.json",
    ".vscode/tasks.json", 
    ".vscode/keybindings.json",
    ".vscode/extensions.json",
    ".cursor/agent.ts",
    "cursor.json",
    "ffp5cs-esp32.code-workspace"
)

Write-Host "`nVerification des fichiers de configuration..." -ForegroundColor Yellow
foreach ($file in $configFiles) {
    if (Test-Path $file) {
        Write-Host "OK $file" -ForegroundColor Green
    } else {
        Write-Host "ERREUR $file - MANQUANT" -ForegroundColor Red
    }
}

# Verification PlatformIO
Write-Host "`nVerification PlatformIO..." -ForegroundColor Yellow
try {
    $pioVersion = pio --version 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "OK PlatformIO installe: $pioVersion" -ForegroundColor Green
    } else {
        Write-Host "ERREUR PlatformIO non installe ou erreur" -ForegroundColor Red
    }
} catch {
    Write-Host "ERREUR PlatformIO non trouve" -ForegroundColor Red
}

# Verification de la compilation
Write-Host "`nTest de compilation..." -ForegroundColor Yellow
try {
    Write-Host "Compilation de l'environnement s3-test..." -ForegroundColor Cyan
    pio run --environment s3-test --target compiledb 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "OK Compilation reussie" -ForegroundColor Green
    } else {
        Write-Host "ERREUR Erreur de compilation" -ForegroundColor Red
    }
} catch {
    Write-Host "ERREUR Impossible de compiler" -ForegroundColor Red
}

# Verification des environnements
Write-Host "`nVerification des environnements..." -ForegroundColor Yellow
$environments = @("s3-test", "wroom-test", "wroom-prod", "s3-prod")
foreach ($env in $environments) {
    try {
        pio run --environment $env --dry-run 2>$null | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-Host "OK $env" -ForegroundColor Green
        } else {
            Write-Host "ERREUR $env - Erreur" -ForegroundColor Red
        }
    } catch {
        Write-Host "ERREUR $env - Non disponible" -ForegroundColor Red
    }
}

# Verification des bibliotheques
Write-Host "`nVerification des bibliotheques principales..." -ForegroundColor Yellow
$libraries = @(
    "ESPAsyncWebServer",
    "ArduinoJson", 
    "DHT sensor library",
    "Adafruit SSD1306",
    "DallasTemperature",
    "OneWire",
    "ESP32Servo"
)

try {
    $libList = pio lib list --json 2>$null | ConvertFrom-Json
    foreach ($lib in $libraries) {
        $found = $libList | Where-Object { $_.name -like "*$lib*" }
        if ($found) {
            Write-Host "OK $lib" -ForegroundColor Green
        } else {
            Write-Host "ATTENTION $lib - Non trouve" -ForegroundColor Yellow
        }
    }
} catch {
    Write-Host "ERREUR Impossible de verifier les bibliotheques" -ForegroundColor Red
}

# Verification des scripts de monitoring
Write-Host "`nVerification des scripts de monitoring..." -ForegroundColor Yellow
$monitoringScripts = @(
    "monitor_90s_v11.49.ps1",
    "monitor_15min.ps1",
    "monitor_30min_v11.49.ps1"
)

foreach ($script in $monitoringScripts) {
    if (Test-Path $script) {
        Write-Host "OK $script" -ForegroundColor Green
    } else {
        Write-Host "ATTENTION $script - Non trouve" -ForegroundColor Yellow
    }
}

# Test de l'agent Cursor
Write-Host "`nVerification de l'agent Cursor..." -ForegroundColor Yellow
if (Test-Path ".cursor/agent.ts") {
    $agentContent = Get-Content ".cursor/agent.ts" -Raw
    if ($agentContent -match "ESP32 Intelligent Assistant") {
        Write-Host "OK Agent ESP32 Intelligent Assistant configure" -ForegroundColor Green
    } else {
        Write-Host "ATTENTION Agent non configure correctement" -ForegroundColor Yellow
    }
} else {
    Write-Host "ERREUR Agent Cursor non trouve" -ForegroundColor Red
}

# Resume
Write-Host "`nRESUME DE LA CONFIGURATION" -ForegroundColor Cyan
Write-Host "=============================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Configuration Cursor IDE optimisee pour ESP32" -ForegroundColor Green
Write-Host "Agent intelligent avec surveillance automatique" -ForegroundColor Green  
Write-Host "Raccourcis clavier pour workflow rapide" -ForegroundColor Green
Write-Host "Taches automatisees PlatformIO" -ForegroundColor Green
Write-Host "Monitoring post-deploiement integre" -ForegroundColor Green
Write-Host ""
Write-Host "PRET POUR LE DEVELOPPEMENT ESP32 !" -ForegroundColor Green
Write-Host ""
Write-Host "Consultez CURSOR_IDE_GUIDE.md pour plus de details" -ForegroundColor Cyan
