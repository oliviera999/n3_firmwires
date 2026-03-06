# Outils Core Dump ESP32

Ce dossier contient les outils pour extraire et analyser les core dumps depuis la partition flash de l'ESP32.

## 📋 Fichiers

- **`extract_coredump.py`** - Extrait le core dump depuis la flash
- **`analyze_coredump.py`** - Analyse un fichier core dump et génère un rapport
- **`coredump_tool.py`** - Outil intégré (extraction + analyse en une commande)
- **`README.md`** - Ce fichier

## 🚀 Utilisation Rapide

### Extraction et Analyse Complète

```bash
# Windows
python tools\coredump\coredump_tool.py --port COM3

# Linux/Mac
python tools/coredump/coredump_tool.py --port /dev/ttyUSB0
```

### Extraction Seulement

```bash
python tools/coredump/extract_coredump.py --port COM3
```

### Analyse d'un Fichier Existant

```bash
python tools/coredump/analyze_coredump.py coredump_20260113_120000.elf
```

## 📖 Documentation Détaillée

### extract_coredump.py

Extrait le core dump depuis la partition flash.

**Options:**
- `--port, -p`: Port série (défaut: COM3)
- `--offset, -o`: Offset de la partition (défaut: 0x3F0000)
- `--size, -s`: Taille de la partition (défaut: 0x10000 = 64 KB)
- `--env`: Environnement test ou prod (défaut: prod)
- `--output, -f`: Fichier de sortie (défaut: coredump_YYYYMMDD_HHMMSS.elf)

**Exemple:**
```bash
python extract_coredump.py --port COM3 --env test
```

### analyze_coredump.py

Analyse un fichier core dump ELF et génère un rapport avec stack trace.

**Options:**
- `coredump_file`: Fichier core dump à analyser (requis)
- `--elf, -e`: Fichier ELF du firmware (cherché automatiquement)
- `--output, -o`: Fichier de sortie pour le rapport
- `--simple, -s`: Utiliser l'analyse simple (sans esp-coredump.py)

**Exemple:**
```bash
python analyze_coredump.py coredump_20260113_120000.elf --elf .pio/build/wroom-test/firmware.elf
```

### coredump_tool.py

Outil intégré qui combine extraction et analyse.

**Options:**
- `--port, -p`: Port série (défaut: COM3)
- `--extract-only`: Extraire seulement, sans analyser
- `--analyze-only FILE`: Analyser seulement un fichier existant
- `--output-dir, -d`: Dossier de sortie (défaut: coredumps/)
- `--keep-raw`: Conserver le fichier brut après analyse

**Exemples:**
```bash
# Extraction et analyse complète
python coredump_tool.py --port COM3

# Extraction seulement
python coredump_tool.py --port COM3 --extract-only

# Analyse d'un fichier existant
python coredump_tool.py --analyze-only coredump_20260113_120000.elf
```

## 🔧 Prérequis

### Outils Requis

1. **esptool.py** - Pour lire la flash
   ```bash
   pip install esptool
   ```

2. **esp-coredump.py** (optionnel, pour analyse complète)
   - Inclus avec ESP-IDF
   - Ou installé séparément: `pip install esp-coredump`

### Fichier ELF du Firmware

Pour une analyse complète avec stack trace, le fichier ELF du firmware compilé est nécessaire.

Le script cherche automatiquement dans `.pio/build/*/firmware.elf`.

Pour compiler le firmware:
```bash
pio run -e wroom-test  # ou wroom-prod
```

## 📊 Interprétation des Résultats

### Fichier Vide (tous 0xFF)

Si le core dump est vide (tous les bytes sont 0xFF), cela signifie qu'aucun crash n'a été enregistré depuis le dernier boot.

### Format ELF Valide

Un fichier ELF valide contient:
- En-tête ELF avec magic bytes `0x7F 'E' 'L' 'F'`
- Informations sur les tâches au moment du crash
- Stack traces pour chaque tâche
- Registres CPU

### Analyse avec Stack Trace

L'analyse complète avec `esp-coredump.py` fournit:
- Liste des tâches actives au moment du crash
- Stack trace pour chaque tâche
- Registres CPU (PC, A0-A15, etc.)
- Adresses mémoire fautives
- Informations sur les exceptions

## 🔍 Dépannage

### "esptool.py non trouvé"

```bash
pip install esptool
```

### "esp-coredump.py non trouvé"

Option 1: Installer ESP-IDF
```bash
# Suivre les instructions: https://docs.espressif.com/projects/esp-idf/
```

Option 2: Utiliser l'analyse simple
```bash
python analyze_coredump.py --simple coredump.elf
```

### "Fichier ELF du firmware non trouvé"

Compilez le projet:
```bash
pio run -e wroom-test
```

Le fichier sera dans `.pio/build/wroom-test/firmware.elf`

### "No core dump partition found!"

Vérifiez que:
1. La partition coredump est configurée dans le fichier de partition
2. Les build flags sont corrects dans `platformio.ini`
3. Le firmware a été compilé avec ces configurations

## 📚 Références

- [Documentation ESP-IDF Core Dump](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/core_dump.html)
- [Guide d'utilisation Core Dump](../docs/guides/COREDUMP_USAGE.md)
- [Rapport d'analyse Core Dump](../docs/reports/RAPPORT_ANALYSE_PARTITION_COREDUMP.md)
