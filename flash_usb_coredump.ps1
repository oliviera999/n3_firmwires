Write-Host "=== FLASHAGE COMPLET FFP5CS AVEC COREDUMP ==="
Write-Host "Ce script va effacer la flash et repartitionner l'ESP32."
Write-Host "Assurez-vous que l'ESP32 est connecté en USB."
Write-Host ""

# Vérification de l'environnement
if (-not (Get-Command "pio" -ErrorAction SilentlyContinue)) {
    Write-Error "PlatformIO (pio) n'est pas trouvé dans le PATH."
    exit 1
}

$env = "wroom-prod"
Write-Host "Environnement cible : $env"
Write-Host "Partition table : partitions_esp32_wroom_ota_coredump.csv"
Write-Host ""

# Confirmation utilisateur
$confirmation = Read-Host "Êtes-vous sûr de vouloir continuer ? (o/N)"
if ($confirmation -ne "o") {
    Write-Host "Annulé."
    exit 0
}

# 1. Compilation
Write-Host "1. Compilation du firmware..."
pio run -e $env
if ($LASTEXITCODE -ne 0) { Write-Error "Erreur de compilation"; exit 1 }

# 2. Erase Flash (Recommandé pour changement de partition)
Write-Host "2. Effacement complet de la flash..."
pio run -t erase -e $env
if ($LASTEXITCODE -ne 0) { Write-Error "Erreur d'effacement"; exit 1 }

# 3. Upload complet (Bootloader + Partitions + Firmware)
Write-Host "3. Écriture du nouveau système de partitions et firmware..."
pio run -t upload -e $env
if ($LASTEXITCODE -ne 0) { Write-Error "Erreur de flashage"; exit 1 }

# 4. Upload Filesystem (LittleFS)
Write-Host "4. Écriture du système de fichiers (LittleFS)..."
pio run -t uploadfs -e $env
if ($LASTEXITCODE -ne 0) { Write-Error "Erreur uploadfs"; exit 1 }

Write-Host ""
Write-Host "=== SUCCÈS ==="
Write-Host "L'ESP32 a été mis à jour avec la nouvelle table de partitions et le support Coredump."
Write-Host "Vous pouvez maintenant surveiller les logs avec : pio run -t monitor -e $env"
