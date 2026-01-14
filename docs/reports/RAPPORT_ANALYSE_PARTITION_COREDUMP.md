# 📋 Rapport d'Analyse - Partition Core Dump ESP32

**Date**: 13 janvier 2026  
**Projet**: FFP5CS - ESP32 Aquaponie Controller  
**Version Firmware**: v11.120+  
**Auteur**: Analyse automatique

---

## 📊 Résumé Exécutif

### État Actuel
- ✅ **Partition Core Dump configurée** dans les fichiers de partition
- ⚠️ **Configuration incohérente** entre les environnements
- ❌ **Pas d'outil d'extraction/analyse** du core dump dans le projet
- ⚠️ **Messages d'erreur** dans les logs indiquant des problèmes de détection

### Recommandations Principales
1. **Harmoniser la configuration** du core dump entre tous les environnements
2. **Créer un outil d'extraction** pour analyser les core dumps sauvegardés
3. **Documenter la procédure** d'utilisation du core dump pour le debugging
4. **Vérifier la compatibilité** des flags de compilation avec la partition table

---

## 🔍 Analyse Détaillée

### 1. Configuration des Partitions

#### 1.1 Partition Test (`partitions_esp32_wroom_test_coredump.csv`)
```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 0x300000,
littlefs, data, littlefs, 0x310000, 0xE0000,
coredump, data, coredump, 0x3F0000, 0x10000,
```

**Caractéristiques**:
- **Taille**: 64 KB (0x10000 = 65536 bytes)
- **Adresse**: 0x3F0000 (dernière partition avant la fin de la flash 4MB)
- **Type**: `data, coredump`
- **Utilisation**: Environnement de test (`wroom-test`)

#### 1.2 Partition Production (`partitions_esp32_wroom_ota_coredump.csv`)
```csv
# Name,Type,SubType,Offset,Size,Flags
nvs,data,nvs,0x9000,0x5000,
otadata,data,ota,0xE000,0x2000,
app0,app,ota_0,0x10000,0x1A0000,
app1,app,ota_1,,0x1A0000,
littlefs,data,littlefs,,0x0A0000,
coredump,data,coredump,,0x010000,
```

**Caractéristiques**:
- **Taille**: 64 KB (0x010000 = 65536 bytes)
- **Adresse**: Automatique (calculée par PlatformIO)
- **Type**: `data, coredump`
- **Utilisation**: Environnement de production (`wroom-prod`)

**⚠️ Problème identifié**: La partition utilise des offsets automatiques (`,,`) ce qui peut causer des problèmes de calcul si les partitions précédentes changent.

---

### 2. Configuration Build Flags

#### 2.1 Environnement Global (`[env]`)
```ini
-DCONFIG_ESP_COREDUMP_ENABLE=1
-DCONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=1
-DCONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=1
```

**Statut**: ✅ Activé globalement

#### 2.2 Environnement Test (`wroom-test`)
```ini
-DCONFIG_ESP_COREDUMP_ENABLE=1
-DCONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=1
-DCONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=1
-DCONFIG_ESP_COREDUMP_CHECKSUM_SHA256=1
```

**Statut**: ✅ Activé avec checksum SHA256

#### 2.3 Environnement Production (`wroom-prod`)
```ini
-DCONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=1
-DCONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=1
-DCONFIG_ESP_COREDUMP_CHECKSUM_SHA256=1
```

**⚠️ Problème identifié**: 
- `CONFIG_ESP_COREDUMP_ENABLE` n'est **pas explicitement défini** dans `wroom-prod`
- Il hérite de `[env]` mais cela peut créer des incohérences

---

### 3. Utilisation dans le Code

#### 3.1 Inclusion Header
**Fichier**: `src/diagnostics.cpp`
```cpp
#include <esp_core_dump.h>
```

**Statut**: ✅ Header inclus

#### 3.2 Utilisation Réelle
**Fichier**: `src/diagnostics.cpp` (lignes 339-344)
```cpp
#if CONFIG_ESP_COREDUMP_ENABLE
report += "Core Dump: ACTIVÉ (Flash)\n";
report += "Note: Utilisez 'pio run -t monitor' ou esp-coredump pour extraire le dump\n";
#else
report += "Core Dump: DÉSACTIVÉ\n";
#endif
```

**Statut**: ⚠️ **Code de rapport uniquement** - Aucune fonction d'extraction/analyse implémentée

#### 3.3 Commentaires dans le Code
**Fichier**: `src/diagnostics.cpp` (lignes 480-488)
```cpp
// Note: Les détails supplémentaires (PC, registres, stack trace) nécessitent :
// 1. Activation du Core Dump dans menuconfig
// 2. Partition dédiée pour stocker le coredump
// 3. Analyse post-mortem avec esp-coredump.py
```

**Statut**: ✅ Documentation présente mais **pas d'implémentation**

---

### 4. Problèmes Identifiés dans les Logs

#### 4.1 Erreur "No core dump partition found!"
```
E (1141) esp_core_dump_flash: No core dump partition found!
E (1142) esp_core_dump_flash: No core dump partition found!
```

**Causes possibles**:
1. **Partition non détectée** par le bootloader ESP-IDF
2. **Incompatibilité** entre les flags de compilation et la partition table
3. **Partition corrompue** ou non initialisée
4. **Bootloader ancien** qui ne reconnaît pas le type `coredump`

#### 4.2 Erreur "Core dump flash config is corrupted!"
```
E (12988) esp_core_dump_flash: Core dump flash config is corrupted!
E (13001) esp_core_dump_common: Core dump write failed with error=-1
```

**Causes possibles**:
1. **Métadonnées corrompues** dans la partition
2. **Checksum invalide** (si SHA256 activé)
3. **Écriture incomplète** lors d'un crash précédent
4. **Conflit de partition** (offset incorrect)

---

### 5. Analyse de Compatibilité

#### 5.1 Format ELF
- ✅ **Format**: ELF (Executable and Linkable Format)
- ✅ **Avantage**: Compatible avec les outils standard (GDB, objdump, esp-coredump.py)
- ✅ **Taille**: Plus compact que le format binaire brut

#### 5.2 Checksum SHA256
- ✅ **Sécurité**: Détection d'erreurs de corruption
- ⚠️ **Overhead**: ~32 bytes supplémentaires par core dump
- ✅ **Recommandé**: Pour la production

#### 5.3 Taille de Partition (64 KB)
- ✅ **Suffisant**: Pour la plupart des core dumps ESP32
- ⚠️ **Limitation**: Peut être insuffisant pour des crashes complexes avec beaucoup de tâches
- 📊 **Recommandation**: 64 KB est un bon compromis pour ESP32-WROOM 4MB

---

### 6. Outils Manquants

#### 6.1 Extraction du Core Dump
**Statut**: ❌ **Aucun outil disponible**

**Recommandation**: Créer un script Python utilisant `esptool.py` pour:
1. Lire la partition coredump depuis la flash
2. Vérifier le checksum SHA256
3. Sauvegarder le fichier ELF localement

#### 6.2 Analyse du Core Dump
**Statut**: ❌ **Aucun outil disponible**

**Recommandation**: Créer un script utilisant `esp-coredump.py` pour:
1. Parser le fichier ELF
2. Extraire la stack trace
3. Identifier la fonction fautive
4. Générer un rapport lisible

#### 6.3 Intégration dans le Workflow
**Statut**: ❌ **Pas d'intégration**

**Recommandation**: 
- Ajouter une commande dans les scripts de monitoring
- Automatiser l'extraction après détection d'un panic
- Générer un rapport automatique

---

## 🔧 Recommandations Techniques

### 1. Harmonisation de la Configuration

#### 1.1 Standardiser les Build Flags
**Action**: Ajouter explicitement tous les flags dans chaque environnement

```ini
# Pour wroom-prod
-DCONFIG_ESP_COREDUMP_ENABLE=1
-DCONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=1
-DCONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=1
-DCONFIG_ESP_COREDUMP_CHECKSUM_SHA256=1
```

#### 1.2 Corriger les Offsets de Partition
**Action**: Remplacer les offsets automatiques par des valeurs explicites

```csv
# partitions_esp32_wroom_ota_coredump.csv
# Calculer les offsets manuellement pour éviter les erreurs
coredump,data,coredump,0x3F0000,0x010000,
```

### 2. Création d'Outils d'Extraction

#### 2.1 Script d'Extraction (`tools/coredump/extract_coredump.py`)
**Fonctionnalités**:
- Connexion série à l'ESP32
- Lecture de la partition coredump via esptool
- Vérification du checksum
- Sauvegarde du fichier ELF avec timestamp

#### 2.2 Script d'Analyse (`tools/coredump/analyze_coredump.py`)
**Fonctionnalités**:
- Utilisation de `esp-coredump.py` (ESP-IDF)
- Parsing du fichier ELF
- Extraction de la stack trace
- Génération d'un rapport markdown

#### 2.3 Script Intégré (`tools/coredump/coredump_tool.py`)
**Fonctionnalités**:
- Extraction + Analyse en une commande
- Intégration avec les symboles du firmware
- Génération de rapport complet

### 3. Documentation

#### 3.1 Guide d'Utilisation
**Fichier**: `docs/guides/COREDUMP_USAGE.md`
**Contenu**:
- Comment activer/désactiver le core dump
- Procédure d'extraction
- Procédure d'analyse
- Interprétation des résultats

#### 3.2 Procédure de Debugging
**Fichier**: `docs/guides/DEBUGGING_WITH_COREDUMP.md`
**Contenu**:
- Workflow complet de debugging avec core dump
- Exemples de stack traces
- Résolution de problèmes courants

### 4. Vérification de la Partition

#### 4.1 Script de Vérification
**Action**: Créer un script pour vérifier:
- Existence de la partition
- Taille correcte
- Type correct
- Offset valide

#### 4.2 Test de Fonctionnement
**Action**: 
- Provoquer un panic volontaire
- Vérifier l'écriture dans la partition
- Extraire et analyser le core dump
- Valider le processus complet

---

## 📈 Impact sur le Projet

### Avantages Actuels
1. ✅ **Partition configurée** - Prête à recevoir des core dumps
2. ✅ **Format ELF** - Compatible avec les outils standard
3. ✅ **Checksum SHA256** - Intégrité des données garantie

### Limitations Actuelles
1. ❌ **Pas d'extraction** - Impossible d'utiliser les core dumps sauvegardés
2. ❌ **Pas d'analyse** - Pas de stack trace disponible
3. ⚠️ **Configuration incohérente** - Risque d'erreurs entre environnements
4. ⚠️ **Messages d'erreur** - Indiquent des problèmes de détection

### Bénéfices Attendus (après corrections)
1. 🎯 **Debugging amélioré** - Stack traces complètes pour identifier les bugs
2. 🎯 **Diagnostic précis** - Compréhension exacte des causes de crash
3. 🎯 **Réduction du temps de résolution** - Identification rapide des problèmes
4. 🎯 **Production-ready** - Outil professionnel de debugging

---

## 🎯 Plan d'Action Recommandé

### Phase 1: Correction Immédiate (Priorité Haute)
1. ✅ **Harmoniser les build flags** dans tous les environnements
2. ✅ **Corriger les offsets** de partition (remplacer `,,` par valeurs explicites)
3. ✅ **Vérifier la compatibilité** partition/build flags

### Phase 2: Outils d'Extraction (Priorité Moyenne)
1. 🔨 **Créer `tools/coredump/extract_coredump.py`**
2. 🔨 **Créer `tools/coredump/analyze_coredump.py`**
3. 🔨 **Tester l'extraction** sur un ESP32 réel

### Phase 3: Documentation (Priorité Moyenne)
1. 📝 **Créer `docs/guides/COREDUMP_USAGE.md`**
2. 📝 **Créer `docs/guides/DEBUGGING_WITH_COREDUMP.md`**
3. 📝 **Mettre à jour la documentation principale**

### Phase 4: Intégration (Priorité Basse)
1. 🔗 **Intégrer dans les scripts de monitoring**
2. 🔗 **Automatiser l'extraction après panic**
3. 🔗 **Générer des rapports automatiques**

---

## 📊 Métriques de Succès

### Critères de Validation
- ✅ **Aucune erreur** "No core dump partition found!" dans les logs
- ✅ **Extraction réussie** d'un core dump depuis la flash
- ✅ **Analyse réussie** avec génération de stack trace
- ✅ **Documentation complète** et à jour

### Tests Requis
1. **Test d'écriture**: Provoquer un panic et vérifier l'écriture
2. **Test d'extraction**: Extraire le core dump depuis la flash
3. **Test d'analyse**: Analyser le core dump et vérifier la stack trace
4. **Test de corruption**: Vérifier la détection d'erreurs (checksum)

---

## 🔗 Références

### Documentation ESP-IDF
- [Core Dump](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/core_dump.html)
- [Partition Tables](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/partition-tables.html)

### Outils
- `esp-coredump.py`: Outil ESP-IDF pour analyser les core dumps
- `esptool.py`: Outil pour lire/écrire la flash ESP32
- `objdump`: Outil standard pour analyser les fichiers ELF

### Fichiers du Projet
- `partitions_esp32_wroom_test_coredump.csv`
- `partitions_esp32_wroom_ota_coredump.csv`
- `platformio.ini`
- `src/diagnostics.cpp`

---

## 📝 Conclusion

La partition core dump est **configurée mais sous-utilisée**. Les problèmes identifiés sont principalement:
1. **Configuration incohérente** entre environnements
2. **Absence d'outils** d'extraction et d'analyse
3. **Messages d'erreur** indiquant des problèmes de détection

**Recommandation principale**: Implémenter les outils d'extraction et d'analyse pour tirer parti de cette fonctionnalité de debugging avancée. Cela permettra d'améliorer significativement la capacité de diagnostic des crashes en production.

---

**Prochaines étapes suggérées**:
1. Corriger la configuration (Phase 1)
2. Créer les outils d'extraction (Phase 2)
3. Tester le processus complet
4. Documenter l'utilisation

---

**Date de création**: 13 janvier 2026  
**Version du rapport**: 1.0  
**Statut**: ✅ Analyse complète
