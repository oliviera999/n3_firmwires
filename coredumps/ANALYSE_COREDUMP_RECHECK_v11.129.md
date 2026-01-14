# 📋 Analyse Core Dump - Nouvelle Extraction v11.129

**Date**: 14 janvier 2026  
**Fichier**: `coredump_20260114_recheck.elf`  
**Taille**: 65536 bytes (64 KB)  
**Comparaison avec**: `coredump_20260114_165909.elf`

---

## 🔍 Résumé Exécutif

### Statut
- ✅ **Core dump extrait avec succès** depuis la partition flash
- ⚠️ **Format différent** détecté par rapport à l'extraction précédente
- ✅ **Signature ELF valide** présente à l'offset 0x18
- ⚠️ **10610 bytes différents** entre les deux extractions

### Format Détecté
- **En-tête**: Magic `e4760000` à l'offset 0x00 (différent de `a47d0000`)
- **Version**: 258 (identique)
- **Fichier ELF**: Signature `7f454c46` à l'offset 0x18 ✅
- **Architecture**: Xtensa (ESP32) - Machine type 0x5E

---

## 📊 Analyse Comparative

### Comparaison des Deux Extractions

| Caractéristique | Extraction 1 (16:59) | Extraction 2 (Recheck) | Statut |
|----------------|---------------------|----------------------|--------|
| **Magic Header** | `a47d0000` | `e4760000` | ⚠️ Différent |
| **Version** | 258 | 258 | ✅ Identique |
| **ELF Signature** | `7f454c46` @0x18 | `7f454c46` @0x18 | ✅ Identique |
| **Format ESP-IDF** | ✅ Valide | ❌ Non standard | ⚠️ |
| **Taille** | 65536 bytes | 65536 bytes | ✅ Identique |
| **Bytes différents** | - | 10610 / 65536 | ⚠️ 16.2% différent |

### Premières Différences

Les différences commencent dès le magic header:
- **Offset 0x0000**: `0xA4` → `0xE4`
- **Offset 0x0001**: `0x7D` → `0x76`
- **Offset 0x0074+**: Différences dans les données

---

## 🔍 Interprétation

### Format Non Standard

Le magic header `e4760000` n'est pas le format ESP-IDF standard (`a47d0000`). Cela peut indiquer:

1. **Format corrompu ou partiellement écrit**
   - Le core dump pourrait être en cours d'écriture
   - Ou la partition a été partiellement effacée

2. **Format différent selon la version ESP-IDF**
   - Différentes versions d'ESP-IDF peuvent utiliser des formats différents
   - Le firmware v11.129 utilise ESP-IDF v5.5

3. **Données invalides**
   - La partition pourrait contenir des données résiduelles
   - Ou un core dump incomplet d'un crash précédent

### Signature ELF Présente

Malgré le magic header différent, la signature ELF est présente à l'offset 0x18, ce qui suggère:
- ✅ Le fichier contient des données structurées
- ✅ Il pourrait être analysable avec `esp-coredump.py`
- ⚠️ Le format pourrait nécessiter un traitement spécial

---

## 🛠️ Recommandations

### 1. Vérifier l'État du Système

Vérifier si un crash s'est produit récemment:
- Consulter les logs série pour des panics/reboots
- Vérifier le compteur de redémarrages dans NVS
- Examiner les raisons de reset

### 2. Tentative d'Analyse avec esp-coredump.py

Même si le format semble non standard, tenter l'analyse:
```bash
esp-coredump.py info_corefile \
    --core coredumps/coredump_20260114_recheck.elf \
    --prog .pio/build/wroom-test/firmware.elf
```

### 3. Extraire à Nouveau Après un Crash

Si un crash se produit:
1. **Attendre le redémarrage complet**
2. **Extraire immédiatement** le core dump
3. **Ne pas redémarrer** l'ESP32 avant l'extraction

### 4. Vérifier la Configuration

Vérifier que:
- Les build flags core dump sont corrects
- La partition est correctement configurée
- Le firmware utilise la bonne version d'ESP-IDF

---

## 📝 Notes Techniques

### Structure Observée

```
Offset 0x00-0x03: e4760000 (Magic header non standard)
Offset 0x04-0x07: 00000102 (Version: 258)
Offset 0x18-0x1B: 7f454c46 (ELF signature ✅)
Offset 0x28-0x29: 5e00 (Machine: Xtensa)
```

### Différences Clés

- **Magic header**: Différent entre les deux extractions
- **Données**: 16.2% du fichier est différent
- **Format**: Le fichier 1 correspond au format ESP-IDF standard, le fichier 2 non

---

## 🔬 Prochaines Étapes

1. **Vérifier les logs série** pour identifier d'éventuels crashes
2. **Tenter l'analyse avec esp-coredump.py** si disponible
3. **Surveiller le système** pour détecter des panics futurs
4. **Documenter le format** si l'analyse révèle des informations utiles

---

## 📊 Conclusion

Le nouveau core dump extrait présente des différences significatives par rapport à l'extraction précédente:
- ⚠️ Format non standard (magic `e4760000`)
- ✅ Signature ELF valide présente
- ⚠️ 16.2% du contenu est différent

**Recommandation**: Vérifier les logs série pour comprendre si un crash s'est produit entre les deux extractions, et tenter une analyse avec `esp-coredump.py` pour voir si le fichier est analysable malgré le format non standard.

---

**Date d'analyse**: 14 janvier 2026  
**Version firmware**: v11.129  
**Environnement**: wroom-test  
**Outils utilisés**: `extract_coredump.py`, `analyze_coredump_detailed.py`, `compare_coredumps.py`
