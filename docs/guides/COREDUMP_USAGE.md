# Guide d'Utilisation - Core Dump ESP32

**Date**: 13 janvier 2026  
**Version**: 1.0  
**Projet**: FFP5CS - ESP32 Aquaponie Controller

---

## 📋 Table des Matières

1. [Introduction](#introduction)
2. [Activation/Désactivation](#activationdésactivation)
3. [Procédure d'Extraction](#procédure-dextraction)
4. [Procédure d'Analyse](#procédure-danalyse)
5. [Interprétation des Résultats](#interprétation-des-résultats)
6. [Dépannage](#dépannage)

---

## 🎯 Introduction

Le **Core Dump** est une fonctionnalité de debugging avancée qui permet de capturer l'état complet du système ESP32 lors d'un crash (panic). Cette information est sauvegardée dans une partition dédiée de la flash et peut être analysée après le redémarrage pour identifier la cause exacte du crash.

### Avantages

- ✅ **Stack trace complète** - Identifie la fonction fautive
- ✅ **État des tâches** - Liste toutes les tâches actives au moment du crash
- ✅ **Registres CPU** - Valeurs des registres au moment de l'exception
- ✅ **Persistance** - Survit aux redémarrages
- ✅ **Production-ready** - Fonctionne même en production

### Limitations

- ⚠️ **Espace flash** - Nécessite 64 KB de partition dédiée
- ⚠️ **Overhead** - Légère augmentation de la taille du firmware
- ⚠️ **Outils requis** - Nécessite esp-coredump.py pour l'analyse complète

---

## ⚙️ Activation/Désactivation

### Configuration Actuelle

Le core dump est **activé** dans les environnements suivants:

- ✅ **wroom-test** - Environnement de test
- ✅ **wroom-prod** - Environnement de production

### Vérifier l'Activation

Pour vérifier si le core dump est activé, consultez les logs au démarrage:

```
[Diagnostics] 🚀 Initialisé - reboot #1, minHeap: 170000 bytes
Core Dump: ACTIVÉ (Flash)
Note: Utilisez 'pio run -t monitor' ou esp-coredump pour extraire le dump
```

### Désactiver le Core Dump

Si vous devez désactiver le core dump (pour économiser l'espace flash):

1. **Modifier `platformio.ini`**:
   ```ini
   build_flags = 
       -DCONFIG_ESP_COREDUMP_ENABLE=0
       -DCONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=0
   ```

2. **Recompiler le firmware**:
   ```bash
   pio run -e wroom-prod
   ```

3. **Flasher l'ESP32**:
   ```bash
   pio run -e wroom-prod -t upload
   ```

### Réactiver le Core Dump

1. **Modifier `platformio.ini`**:
   ```ini
   build_flags = 
       -DCONFIG_ESP_COREDUMP_ENABLE=1
       -DCONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=1
       -DCONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=1
       -DCONFIG_ESP_COREDUMP_CHECKSUM_SHA256=1
   ```

2. **Vérifier la partition** dans le fichier de partition CSV:
   ```csv
   coredump,data,coredump,0x3F0000,0x010000,
   ```

3. **Recompiler et flasher**

---

## 📥 Procédure d'Extraction

### Méthode 1: Outil Intégré (Recommandé)

L'outil intégré combine extraction et analyse:

```bash
# Windows
python tools\coredump\coredump_tool.py --port COM3

# Linux/Mac
python tools/coredump/coredump_tool.py --port /dev/ttyUSB0
```

**Options disponibles:**
- `--port COM3`: Port série de l'ESP32
- `--env prod`: Environnement (prod ou test)
- `--extract-only`: Extraire seulement, sans analyser
- `--output-dir coredumps`: Dossier de sortie

### Méthode 2: Script d'Extraction Seul

```bash
python tools/coredump/extract_coredump.py --port COM3
```

**Options:**
- `--port COM3`: Port série
- `--offset 0x3F0000`: Offset de la partition (défaut: 0x3F0000)
- `--size 0x10000`: Taille de la partition (défaut: 64 KB)
- `--env prod`: Environnement (test ou prod)
- `--output coredump.elf`: Fichier de sortie

### Méthode 3: esptool.py Manuel

Si vous préférez utiliser esptool directement:

```bash
esptool.py --port COM3 --baud 921600 read_flash 0x3F0000 0x10000 coredump.elf
```

### Vérification de l'Extraction

Après l'extraction, le script vérifie automatiquement le fichier:

```
✅ Fichier ELF valide (65536 bytes)
```

Si le fichier est vide (tous 0xFF), cela signifie qu'aucun crash n'a été enregistré.

---

## 📊 Procédure d'Analyse

### Méthode 1: Analyse Automatique (Recommandée)

L'outil intégré analyse automatiquement après l'extraction:

```bash
python tools/coredump/coredump_tool.py --port COM3
```

### Méthode 2: Analyse d'un Fichier Existant

Si vous avez déjà extrait le core dump:

```bash
python tools/coredump/analyze_coredump.py coredump_20260113_120000.elf
```

**Options:**
- `--elf firmware.elf`: Fichier ELF du firmware (cherché automatiquement)
- `--output rapport.txt`: Fichier de sortie pour le rapport
- `--simple`: Analyse simple sans esp-coredump.py

### Méthode 3: esp-coredump.py Manuel

Pour une analyse manuelle avec esp-coredump.py:

```bash
esp-coredump.py info_corefile \
    --core coredump.elf \
    --core-format elf \
    --prog .pio/build/wroom-prod/firmware.elf
```

### Prérequis pour l'Analyse Complète

Pour obtenir une stack trace complète, vous avez besoin:

1. **Fichier ELF du firmware** - Compilé avec les mêmes options que le firmware en production
   ```bash
   pio run -e wroom-prod
   # Fichier: .pio/build/wroom-prod/firmware.elf
   ```

2. **esp-coredump.py** - Outil ESP-IDF
   - Inclus avec ESP-IDF
   - Ou: `pip install esp-coredump`

---

## 🔍 Interprétation des Résultats

### Rapport d'Analyse

Un rapport d'analyse typique contient:

```
============================================================
📋 Rapport d'Analyse Core Dump
============================================================
Fichier: coredump_20260113_120000.elf
Date: 2026-01-13 12:00:00

Taille du fichier: 65536 bytes (64.00 KB)
Format: ELF valide ✅
Architecture: Xtensa (ESP32)

============================================================
Tâches au moment du crash:
============================================================
1. auto_task (Stack: 8192 bytes, Utilisé: 2048 bytes)
   PC: 0x400d1234
   Stack trace:
   0x400d1234: automationTask() at src/app_tasks.cpp:123
   0x400d1000: vTaskFunction() at FreeRTOS/tasks.c:456

2. loopTask (Stack: 4096 bytes, Utilisé: 1024 bytes)
   PC: 0x400e5678
   Stack trace:
   0x400e5678: loop() at src/main.cpp:45
```

### Informations Clés

1. **PC (Program Counter)** - Adresse de l'instruction qui a causé le crash
2. **Stack Trace** - Liste des fonctions appelées (de la plus récente à la plus ancienne)
3. **Tâches** - Toutes les tâches actives au moment du crash
4. **Registres** - Valeurs des registres CPU (A0-A15, etc.)

### Exemple d'Interprétation

Si le rapport montre:

```
PC: 0x400d1234: automationTask() at src/app_tasks.cpp:123
```

Cela signifie que le crash s'est produit dans la fonction `automationTask()` à la ligne 123 du fichier `src/app_tasks.cpp`.

### Fichier Vide

Si le core dump est vide (tous 0xFF):

```
⚠️  ATTENTION: Partition vide (tous les bytes sont 0xFF)
   Cela signifie qu'aucun crash n'a été enregistré.
```

**Causes possibles:**
- Aucun crash depuis le dernier boot
- Le firmware a été reflashé (partition effacée)
- Le core dump a été effacé manuellement

---

## 🔧 Dépannage

### Erreur: "No core dump partition found!"

**Cause**: La partition n'est pas détectée par le bootloader.

**Solutions**:
1. Vérifier que la partition est dans le fichier CSV:
   ```csv
   coredump,data,coredump,0x3F0000,0x010000,
   ```

2. Vérifier les build flags dans `platformio.ini`:
   ```ini
   -DCONFIG_ESP_COREDUMP_ENABLE=1
   -DCONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=1
   ```

3. Recompiler et reflasher le firmware complet (pas seulement l'app)

### Erreur: "esptool.py non trouvé"

**Solution**:
```bash
pip install esptool
```

### Erreur: "esp-coredump.py non trouvé"

**Solutions**:

1. **Installer ESP-IDF** (recommandé):
   - Suivre: https://docs.espressif.com/projects/esp-idf/

2. **Utiliser l'analyse simple**:
   ```bash
   python analyze_coredump.py --simple coredump.elf
   ```

3. **Installer esp-coredump séparément**:
   ```bash
   pip install esp-coredump
   ```

### Erreur: "Fichier ELF du firmware non trouvé"

**Solution**:
1. Compiler le projet:
   ```bash
   pio run -e wroom-prod
   ```

2. Spécifier le fichier ELF manuellement:
   ```bash
   python analyze_coredump.py coredump.elf --elf .pio/build/wroom-prod/firmware.elf
   ```

### Core Dump Toujours Vide

**Causes possibles**:
1. Aucun crash depuis le dernier boot
2. Le firmware a été reflashé
3. La partition a été effacée

**Vérification**:
- Vérifier les logs pour des panics/reboots
- Provoquer un crash volontaire pour tester
- Vérifier que le core dump est activé dans les build flags

### Stack Trace Incomplète

**Causes possibles**:
1. Fichier ELF du firmware non fourni ou incorrect
2. Firmware compilé avec des options différentes
3. Symbols strippés (optimisation -Os)

**Solutions**:
1. Utiliser le fichier ELF exact du firmware en production
2. Compiler avec les mêmes options que le firmware déployé
3. Vérifier que les symbols ne sont pas strippés

---

## 📚 Références

- [Rapport d'Analyse Core Dump](../reports/RAPPORT_ANALYSE_PARTITION_COREDUMP.md)
- [Documentation ESP-IDF Core Dump](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/core_dump.html)
- [Outils Core Dump](../../tools/coredump/README.md)

---

**Date de création**: 13 janvier 2026  
**Version**: 1.0  
**Auteur**: Assistant IA
