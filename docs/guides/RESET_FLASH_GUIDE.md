# Guide de Réinitialisation de la Mémoire Flash ESP32

## 📋 Vue d’ensemble

Ce guide explique comment faire un reset complet ou partiel de la mémoire flash de l'ESP32 dans le projet FFP3.

## 🔧 Outils disponibles

### 1. Script interactif complet
```bash
python reset_flash_esp32.py
```
- Interface interactive avec menu
- Options multiples de reset
- Vérification des ports automatique

### 2. Script rapide en ligne de commande
```bash
python quick_reset_esp32.py --complete    # Reset complet
python quick_reset_esp32.py --nvs         # Reset NVS seulement
python quick_reset_esp32.py --ota         # Reset OTA seulement
python quick_reset_esp32.py --fs          # Reset système de fichiers
```

### 3. Commandes PlatformIO directes
```bash
pio run -t erase          # Effacement complet de la flash
pio run -t upload         # Upload du firmware
pio run -t uploadfs       # Upload du système de fichiers
```

## 🎯 Types de Reset

### 🔥 Réinitialisation complète
**Efface TOUTE la mémoire flash et recharge le firmware**

**Quand l'utiliser :**
- Problèmes graves de corruption
- Changement de configuration majeure
- Debugging de problèmes persistants

**Ce qui est effacé :**
- Firmware complet
- Données NVS (configuration)
- Données OTA
- Système de fichiers

**Commande :**
```bash
python quick_reset_esp32.py --complete
```

### 🗂️ Réinitialisation NVS seulement
**Efface uniquement la configuration stockée**

**Quand l'utiliser :**
- Problèmes de configuration
- Reset des paramètres WiFi
- Corruption des données de configuration

**Ce qui est effacé :**
- Configuration WiFi
- Paramètres utilisateur
- Données de calibration

**Commande :**
```bash
python quick_reset_esp32.py --nvs
```

### 🔄 Réinitialisation OTA seulement
**Efface les données de mise à jour OTA**

**Quand l'utiliser :**
- Problèmes de mise à jour OTA
- Corruption des données OTA
- Reset des flags de mise à jour

**Ce qui est effacé :**
- Données de mise à jour OTA
- Flags de mise à jour

**Commande :**
```bash
python quick_reset_esp32.py --ota
```

### 📁 Réinitialisation du système de fichiers
**Efface le système de fichiers LittleFS**

**Quand l'utiliser :**
- Problèmes de fichiers
- Corruption du système de fichiers
- Reset des données stockées

**Ce qui est effacé :**
- Fichiers de configuration
- Logs stockés
- Données utilisateur

**Commande :**
```bash
python quick_reset_esp32.py --fs
```

## ⚠️ Précautions importantes

### Avant un reset complet
1. **Sauvegardez vos données importantes**
2. **Notez vos configurations WiFi**
3. **Assurez-vous d'avoir le bon firmware**
4. **Vérifiez que l'ESP32 est bien connecté**

### Pendant le reset
1. **Ne débranchez pas l'ESP32**
2. **Attendez la fin de chaque étape**
3. **Vérifiez les messages d'erreur**

### Après le reset
1. **Reconfigurez le WiFi**
2. **Vérifiez le fonctionnement**
3. **Testez les fonctionnalités principales**

## 🔍 Dépannage

### Erreur "Device not found"
```bash
# Vérifiez les ports disponibles
pio device list

# Ou utilisez le script interactif
python reset_flash_esp32.py
# Puis choisissez option 5
```

### Erreur "Upload failed"
```bash
# Essayez un reset complet
python quick_reset_esp32.py --complete

# Ou utilisez esptool directement
esptool.py --chip esp32s3 erase_flash
esptool.py --chip esp32s3 --port COM3 write_flash 0x0 firmware.bin
```

### Erreur "Permission denied"
```bash
# Sur Linux/Mac, utilisez sudo
sudo python quick_reset_esp32.py --complete

# Sur Windows, lancez en tant qu'administrateur
```

## 📊 Structure de la mémoire flash ESP32-S3

```
0x00000000 - 0x0000FFFF: Bootloader (64KB)
0x00010000 - 0x0008FFFF: Partition Table (512KB)
0x00090000 - 0x000EFFFF: NVS (384KB)
0x000F0000 - 0x0014FFFF: OTA Data (384KB)
0x00150000 - 0x0024FFFF: OTA App (1MB)
0x00250000 - 0x0034FFFF: OTA App (1MB)
0x00350000 - 0x0044FFFF: Filesystem (1MB)
```

## 🚀 Commandes rapides utiles

```bash
# Compiler le projet
pio run

# Flasher le firmware
pio run -t upload

# Ouvrir le moniteur série
pio device monitor

# Nettoyer le projet
pio run -t clean

# Voir les tâches disponibles
pio run --list-targets
```

## 📞 Support

En cas de problème :
1. Vérifiez que l'ESP32 est bien connecté
2. Essayez un reset complet
3. Consultez les logs du moniteur série
4. Vérifiez la documentation PlatformIO

## 🔗 Liens utiles

- [Documentation PlatformIO](https://docs.platformio.org/)
- [Guide ESP32 Flash](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/bootloader.html)
- [ESP32-S3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf) 