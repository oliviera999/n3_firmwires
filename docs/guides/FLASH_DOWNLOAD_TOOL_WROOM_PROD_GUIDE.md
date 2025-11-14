# Guide Flash Download Tool - ESP32-WROOM-PROD

## 📋 Vue d'ensemble

Ce guide explique comment flasher un ESP32 complètement vierge avec **Flash Download Tool** en utilisant les fichiers `.bin` générés par PlatformIO pour l'environnement **wroom-prod**.

## 🎯 Prérequis

### 1. Compilation du projet
Assurez-vous d'avoir compilé le projet pour l'environnement `wroom-prod` :

```bash
pio run -e wroom-prod
```

### 2. Fichiers générés
Après compilation, les fichiers suivants seront disponibles dans `.pio/build/wroom-prod/` :

- `bootloader.bin` - Bootloader ESP32
- `partitions.bin` - Table de partitions
- `boot_app0.bin` - Helper de démarrage OTA (optionnel mais recommandé)
- `firmware.bin` - Application principale (app0)
- `littlefs.bin` - Système de fichiers LittleFS (généré avec `pio run -e wroom-prod -t buildfs`)

### 3. Téléchargement de Flash Download Tool
- Téléchargez depuis : https://www.espressif.com/en/support/download/other-tools
- Version recommandée : **v3.x** ou supérieure
- L'outil ne nécessite pas d'installation, il suffit de l'extraire

## 📊 Table de partition - wroom-prod

Le fichier `partitions_esp32_wroom_ota_fs_medium.csv` définit la structure suivante :

```csv
# Name,Type,SubType,Offset,Size,Flags
nvs,data,nvs,0x9000,0x5000
otadata,data,ota,0xE000,0x2000
app0,app,ota_0,0x10000,0x1A0000
app1,app,ota_1,,0x1A0000
littlefs,data,littlefs,,0x0B0000
```

### Calcul des adresses

| Partition | Offset | Taille | Description |
|-----------|--------|--------|-------------|
| **Bootloader** | `0x1000` | ~32 KB | Bootloader ESP32 (fixe) |
| **Partition Table** | `0x8000` | ~3 KB | Table de partitions (fixe) |
| **NVS** | `0x9000` | 20 KB | Non-volatile storage |
| **OTA Data** | `0xE000` | 8 KB | Métadonnées OTA |
| **Boot App0** | `0xE000` | ~4 KB | Helper de démarrage (optionnel) |
| **App0** | `0x10000` | 1.625 MB | Application principale (firmware) |
| **App1** | `0x1B0000` | 1.625 MB | Application OTA (backup) |
| **LittleFS** | `0x350000` | 704 KB | Système de fichiers |

**Calculs :**
- App1 offset = `0x10000 + 0x1A0000 = 0x1B0000`
- LittleFS offset = `0x1B0000 + 0x1A0000 = 0x350000`

## 🔧 Configuration Flash Download Tool

### Étape 1 : Lancement de l'outil

1. Lancez `flash_download_tool_*.exe` (Windows) ou l'équivalent Linux/Mac
2. Sélectionnez **ESP32** comme type de puce
3. Choisissez **Develop** mode
4. Sélectionnez **UART** comme mode de chargement

### Étape 2 : Configuration SPI Flash

Dans l'onglet **SPI Download**, configurez :

| Paramètre | Valeur |
|-----------|--------|
| **SPI Speed** | `40 MHz` |
| **SPI Mode** | `DIO` |
| **Flash Size** | `4MB (32Mbit)` |
| **COM Port** | Sélectionnez le port COM de votre ESP32 |

### Étape 3 : Ajout des fichiers binaires

Ajoutez les fichiers dans l'ordre suivant avec leurs adresses exactes :

#### Configuration minimale (firmware uniquement)

| Fichier | Adresse | Taille typique | Description |
|---------|---------|----------------|-------------|
| `bootloader.bin` | `0x1000` | ~32 KB | **OBLIGATOIRE** - Bootloader |
| `partitions.bin` | `0x8000` | ~3 KB | **OBLIGATOIRE** - Table de partitions |
| `boot_app0.bin` | `0xE000` | ~4 KB | **RECOMMANDÉ** - Helper OTA |
| `firmware.bin` | `0x10000` | ~1.6 MB | **OBLIGATOIRE** - Application |

#### Configuration complète (firmware + filesystem)

| Fichier | Adresse | Taille typique | Description |
|---------|---------|----------------|-------------|
| `bootloader.bin` | `0x1000` | ~32 KB | **OBLIGATOIRE** - Bootloader |
| `partitions.bin` | `0x8000` | ~3 KB | **OBLIGATOIRE** - Table de partitions |
| `boot_app0.bin` | `0xE000` | ~4 KB | **RECOMMANDÉ** - Helper OTA |
| `firmware.bin` | `0x10000` | ~1.6 MB | **OBLIGATOIRE** - Application |
| `littlefs.bin` | `0x350000` | ~400-700 KB | **OPTIONNEL** - Filesystem (interface web) |

### Étape 4 : Procédure d'ajout des fichiers

Pour chaque fichier :

1. Cliquez sur **[...]** à droite de la ligne
2. Naviguez vers `.pio/build/wroom-prod/`
3. Sélectionnez le fichier `.bin` correspondant
4. Vérifiez que l'adresse est correcte
5. **Cochez la case** pour activer le flash de ce fichier
6. Répétez pour tous les fichiers nécessaires

### Exemple de configuration complète

```
┌─────────────────────────────────────────────────────────────┐
│ SPI Download Configuration                                  │
├──────┬──────────────────────────────────┬────────┬──────────┤
│ ☑    │ bootloader.bin                  │ 0x1000 │          │
│ ☑    │ partitions.bin                  │ 0x8000 │          │
│ ☑    │ boot_app0.bin                   │ 0xE000 │          │
│ ☑    │ firmware.bin                    │ 0x10000│          │
│ ☑    │ littlefs.bin                    │ 0x350000│         │
└──────┴──────────────────────────────────┴────────┴──────────┘

SPI Speed: 40 MHz
SPI Mode: DIO
Flash Size: 4MB (32Mbit)
COM Port: COM3 (ou votre port)
```

## 🚀 Procédure de flash

### Préparation de l'ESP32

1. **Connectez l'ESP32** à votre PC via USB
2. **Identifiez le port COM** :
   - Windows : Gestionnaire de périphériques → Ports (COM et LPT)
   - Linux : `ls /dev/ttyUSB*` ou `ls /dev/ttyACM*`
   - Mac : `ls /dev/cu.usbserial*`

3. **Mise en mode download** (si nécessaire) :
   - **Méthode 1 (automatique)** : L'outil peut le faire automatiquement via DTR/RTS
   - **Méthode 2 (manuelle)** :
     - Maintenez le bouton **BOOT** enfoncé
     - Appuyez brièvement sur **RESET**
     - Relâchez **BOOT**

### Lancement du flash

1. **Vérifiez la configuration** :
   - Tous les fichiers sont cochés
   - Les adresses sont correctes
   - Le port COM est sélectionné
   - Les paramètres SPI sont corrects

2. **Cliquez sur START** :
   - La barre de progression s'affiche
   - Le processus prend 30-60 secondes selon la taille des fichiers
   - Attendez la fin complète du processus

3. **Vérification** :
   - Message "FINISH" ou "SUCCESS" à la fin
   - Pas d'erreurs dans la console
   - L'ESP32 redémarre automatiquement

### Flash du filesystem (optionnel, après firmware)

Si vous n'avez pas flashé `littlefs.bin` avec le firmware, vous pouvez le faire séparément :

1. **Générer le filesystem** :
   ```bash
   pio run -e wroom-prod -t buildfs
   ```

2. **Flasher uniquement le filesystem** :
   - Dans Flash Download Tool, **décochez** tous les fichiers sauf `littlefs.bin`
   - Adresse : `0x350000`
   - Cliquez sur **START**

## ⚠️ Points d'attention

### 1. Ordre des fichiers
- **IMPORTANT** : Flash le `bootloader.bin` et `partitions.bin` en premier
- Le `firmware.bin` doit être flashé à l'adresse exacte `0x10000`

### 2. Taille du firmware
- Vérifiez que le firmware ne dépasse pas **1.625 MB** (0x1A0000)
- Si le firmware est trop gros, la compilation échouera ou le flash échouera

### 3. Filesystem
- Le filesystem est **optionnel** mais nécessaire pour l'interface web
- Si vous ne flashez pas le filesystem, l'ESP32 fonctionnera mais sans interface web
- Vous pouvez le flasher plus tard via OTA ou Flash Download Tool

### 4. Erase Flash
- Pour un ESP32 **complètement vierge**, vous pouvez d'abord effacer la flash :
  - Dans Flash Download Tool : Onglet **ERASE**
  - Ou utilisez esptool : `esptool.py --chip esp32 erase_flash`

### 5. Adresses fixes vs calculées
- Les adresses `0x1000`, `0x8000`, `0xE000`, `0x10000` sont **fixes** (standard ESP32)
- Les adresses `0x1B0000` (app1) et `0x350000` (littlefs) sont **calculées** à partir de la partition table

## 🔍 Vérification post-flash

### 1. Monitoring série

Connectez-vous au port série pour vérifier le démarrage :

```bash
pio device monitor -e wroom-prod
# Ou
pio device monitor --baud 115200 --port COM3
```

**Logs attendus :**
```
ESP32 Aquaponie Controller - FFP5CS
Version: X.XX
Board: ESP32-WROOM
...
WiFi connecting...
...
```

### 2. Vérification des partitions

Les logs devraient afficher :
```
[Partition] App0: 0x10000, size: 0x1A0000
[Partition] App1: 0x1B0000, size: 0x1A0000
[Partition] LittleFS: 0x350000, size: 0x0B0000
```

### 3. Accès à l'interface web

1. Connectez-vous au WiFi de l'ESP32 (point d'accès par défaut)
2. Ou connectez l'ESP32 à votre réseau WiFi
3. Accédez à `http://192.168.4.1` (AP mode) ou l'IP attribuée

## 🐛 Dépannage

### Erreur "Failed to connect"

**Solutions :**
- Vérifiez le port COM (peut changer après débranchement)
- Vérifiez les drivers USB (CH340, CP2102, etc.)
- Essayez un autre câble USB
- Mettez manuellement l'ESP32 en mode download

### Erreur "Invalid address" ou "Address out of range"

**Solutions :**
- Vérifiez que les adresses sont en **hexadécimal** (0x...)
- Vérifiez que la Flash Size est bien **4MB**
- Vérifiez que les adresses correspondent à la partition table

### Erreur "Failed to write flash"

**Solutions :**
- Réduisez le **SPI Speed** à 20 MHz ou moins
- Vérifiez les connexions USB
- Essayez un autre câble USB (qualité)
- Vérifiez que l'ESP32 n'est pas en mode sleep

### Firmware ne démarre pas

**Solutions :**
- Vérifiez que `bootloader.bin` et `partitions.bin` ont été flashés
- Vérifiez que `firmware.bin` est à l'adresse `0x10000`
- Vérifiez les logs série pour les erreurs
- Essayez un flash complet (tous les fichiers)

### Filesystem non détecté

**Solutions :**
- Vérifiez que `littlefs.bin` est à l'adresse `0x350000`
- Vérifiez que le filesystem a été généré (`pio run -e wroom-prod -t buildfs`)
- Vérifiez les logs : `LittleFS mount failed` indique un problème

## 📝 Résumé des commandes

### Compilation et génération

```bash
# Compiler le firmware
pio run -e wroom-prod

# Générer le filesystem
pio run -e wroom-prod -t buildfs

# Compiler + générer filesystem
pio run -e wroom-prod -t buildfs && pio run -e wroom-prod
```

### Emplacement des fichiers

```
.pio/build/wroom-prod/
├── bootloader.bin          → 0x1000
├── partitions.bin          → 0x8000
├── boot_app0.bin           → 0xE000
├── firmware.bin            → 0x10000
└── littlefs.bin            → 0x350000 (après buildfs)
```

### Configuration Flash Download Tool

| Paramètre | Valeur |
|-----------|--------|
| Chip Type | ESP32 |
| Work Mode | Develop |
| Load Mode | UART |
| SPI Speed | 40 MHz |
| SPI Mode | DIO |
| Flash Size | 4MB (32Mbit) |

## ✅ Checklist de flash

- [ ] Projet compilé avec `pio run -e wroom-prod`
- [ ] Filesystem généré avec `pio run -e wroom-prod -t buildfs` (si nécessaire)
- [ ] Flash Download Tool téléchargé et lancé
- [ ] Port COM identifié et sélectionné
- [ ] Paramètres SPI configurés (40 MHz, DIO, 4MB)
- [ ] `bootloader.bin` ajouté à `0x1000` et coché
- [ ] `partitions.bin` ajouté à `0x8000` et coché
- [ ] `boot_app0.bin` ajouté à `0xE000` et coché (recommandé)
- [ ] `firmware.bin` ajouté à `0x10000` et coché
- [ ] `littlefs.bin` ajouté à `0x350000` et coché (optionnel)
- [ ] ESP32 en mode download (si nécessaire)
- [ ] Flash lancé et terminé avec succès
- [ ] Monitoring série pour vérifier le démarrage
- [ ] Interface web accessible (si filesystem flashé)

## 📚 Références

- **Table de partition** : `partitions_esp32_wroom_ota_fs_medium.csv`
- **Configuration PlatformIO** : `platformio.ini` → `[env:wroom-prod]`
- **Flash Download Tool** : https://www.espressif.com/en/support/download/other-tools
- **Documentation ESP32** : https://docs.espressif.com/projects/esp-idf/en/latest/

---

**Date de création** : 2025-01-XX  
**Version** : 1.0  
**Environnement** : wroom-prod  
**Table de partition** : partitions_esp32_wroom_ota_fs_medium.csv




