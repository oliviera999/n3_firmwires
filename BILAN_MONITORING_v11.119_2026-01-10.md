# Bilan Monitoring ESP32 v11.119
**Date**: 10 janvier 2026  
**Durée du monitoring**: 90 secondes  
**Log analysé**: `monitor_90s_v11.119_2026-01-10_10-04-53.log` (1787 lignes)

---

## 🔴 PROBLÈMES CRITIQUES DÉTECTÉS

### 1. **Stack Overflow dans `autoTask` (automationTask)**
**Ligne 1501-1502**: `Guru Meditation Error: Core 1 panic'ed (Unhandled debug exception)`
- **Type**: Stack canary watchpoint triggered
- **Tâche**: `autoTask` (automationTask)
- **Cause probable**: Stack insuffisant (4096 bytes seulement)
- **Impact**: **CRITIQUE** - Le système reboot après le crash

**Détails techniques**:
```
Debug exception reason: Stack canary watchpoint triggered (autoTask)
Core 1 register dump:
PC      : 0x40085170  PS      : 0x00060636  A0      : 0x80083414
```

**Configuration actuelle** (`include/config.h:439`):
```cpp
constexpr uint32_t AUTOMATION_TASK_STACK_SIZE = 4096;  // ❌ INSUFFISANT
```

**Recommandation**: 
- ⚠️ **URGENT**: Augmenter `AUTOMATION_TASK_STACK_SIZE` à **8192 bytes minimum** (ou 9216 comme prévu dans la documentation)
- Vérifier la High Water Mark (HWM) de la stack après modification

---

### 2. **Watchdog Timeout - IDLE1 (CPU 1)**
**Ligne 7-12**: Task watchdog got triggered
- **Tâches bloquantes**: 
  - CPU 0: `async_tcp`
  - CPU 1: `loopTask`
- **Cause**: Le CPU 1 (IDLE1) n'a pas pu reset le watchdog à temps
- **Impact**: **CRITIQUE** - Reboot immédiat du système

**Détails**:
```
E (84045) task_wdt: Task watchdog got triggered.
E (84046) task_wdt:  - IDLE1 (CPU 1)
E (84046) task_wdt: Tasks currently running:
E (84046) task_wdt: CPU 0: async_tcp
E (84046) task_wdt: CPU 1: loopTask
E (84058) task_wdt: Aborting.
```

**Recommandation**:
- ⚠️ Vérifier que toutes les tâches critiques appellent `esp_task_wdt_reset()` régulièrement
- Vérifier que `loopTask` ne reste pas bloquante trop longtemps (> 3 secondes)
- Vérifier la gestion du watchdog dans `async_tcp`

---

### 3. **Core Dump Partition Manquante/Corrompue**
**Lignes 25-27, 1519-1521, 1557**: Core dump write failed
- **Erreur**: `Core dump flash config is corrupted! CRC=0x7bd5c66f instead of 0x0`
- **Impact**: **MOYEN** - Pas de core dump disponible pour analyse post-crash

**Détails**:
```
E (12988) esp_core_dump_flash: Core dump flash config is corrupted!
E (13001) esp_core_dump_common: Core dump write failed with error=-1
E (1124) esp_core_dump_flash: No core dump partition found!
```

**Recommandation**:
- ⚠️ Vérifier la configuration des partitions dans `platformio.ini` et `partitions_*.csv`
- Ajouter une partition coredump si nécessaire (pour debug uniquement)
- Ou désactiver complètement le core dump en production (économise la flash)

---

## 🟡 PROBLÈMES MOYENS DÉTECTÉS

### 4. **Erreurs NVS au Démarrage**
**Lignes 137-142**: Erreurs création namespace NVS
- **Erreurs**: Tous les namespaces retournent l'erreur 1 (déjà existants ?)
- **Impact**: **FAIBLE** - Le système fonctionne malgré ces erreurs (avertissements seulement)

**Détails**:
```
[NVS] ⚠️ Erreur création namespace sys: 1
[NVS] ⚠️ Erreur création namespace cfg: 1
[NVS] ⚠️ Erreur création namespace time: 1
[NVS] ⚠️ Erreur création namespace state: 1
[NVS] ⚠️ Erreur création namespace logs: 1
[NVS] ⚠️ Erreur création namespace sens: 1
```

**Recommandation**:
- Vérifier la logique de création des namespaces (peut-être qu'ils existent déjà)
- Changer les messages d'erreur en messages d'information si c'est normal

---

### 5. **OLED Non Détectée**
**Lignes 194-200, 1685-1689**: I2C transaction failed pour OLED
- **Erreur**: `I2C hardware NACK detected` ou `I2C transaction timeout`
- **Impact**: **FAIBLE** - Le système passe en mode dégradé (pas d'affichage)

**Détails**:
```
E (1192) i2c.master: I2C hardware NACK detected
E (1193) i2c.master: I2C transaction unexpected nack detected
[OLED] Non détectée sur I2C (SDA:21, SCL:22, ADDR:0x3C)
[OLED] Mode dégradé activé (pas d'affichage)
```

**Recommandation**:
- Vérifier le câblage I2C de l'OLED (SDA=21, SCL=22)
- Vérifier que l'OLED est alimentée correctement
- Vérifier les pull-ups I2C

---

## 🟢 POINTS POSITIFS

### 6. **WiFi Fonctionne Correctement**
**Lignes 591, 1725**: WiFi connecté avec succès
- **SSID**: AP-Techno-T06
- **IP**: 192.168.42.32
- **RSSI**: -34 dBm à -59 dBm (bon signal)
- **Connexions**: 12 occurrences de "WiFi connected" pendant le monitoring

**Détails**:
```
[WiFi] ✅ Connecté à AP-Techno-T06 (192.168.42.32, RSSI -34 dBm)
[GET] WiFi Status: 3 (connected=YES)
```

---

### 7. **Mémoire Heap Stable**
**Lignes 751, 1205, 1484, 1771**: Free heap varie mais reste acceptable
- **Heap libre**: 114 KB à 171 KB
- **Tendance**: Diminution progressive (170 KB → 139 KB → 114 KB) puis remontée après reboot (171 KB)
- **Impact**: **ACCEPTABLE** - Pas de leak mémoire évident

**Détails**:
```
[GET] Free heap: 170304 bytes  (≈ 166 KB)
[GET] Free heap: 139712 bytes  (≈ 136 KB)
[GET] Free heap: 114816 bytes  (≈ 112 KB)
[GET] Free heap: 171380 bytes  (≈ 167 KB - après reboot)
```

**Note**: La diminution progressive pourrait indiquer une légère fragmentation, mais ce n'est pas critique.

---

### 8. **Synchronisation NTP Fonctionne**
**Ligne 1732**: Synchronisation NTP réussie
- **Serveur**: pool.ntp.org
- **Latence**: 1 ms
- **Heure**: 09:52:37 10/01/2026

**Détails**:
```
[09:52:37][INFO][NTP] Synchronisation NTP réussie en 1 ms (0 tentatives)
[09:52:37][INFO][NTP] Heure NTP: 09:52:37 10/01/2026 (epoch: 0)
```

---

### 9. **Système Redémarre Correctement**
**Lignes 28, 1522**: Après chaque crash, le système redémarre automatiquement
- **Type de reset**: `SW_CPU_RESET` (software reset)
- **Boot**: `SPI_FAST_FLASH_BOOT`
- **Impact**: **POSITIF** - Le système est résilient et continue de fonctionner malgré les crashes

---

## 📊 STATISTIQUES GLOBALES

| Métrique | Valeur | État |
|----------|--------|------|
| **Durée monitoring** | 90 secondes | ✅ |
| **Lignes de log** | 1787 | ✅ |
| **Crashes détectés** | 2 (Guru Meditation) | 🔴 |
| **Watchdog timeouts** | 1 | 🔴 |
| **Core dumps échoués** | 7 | 🟡 |
| **Connexions WiFi** | 12 | ✅ |
| **Heap libre min** | 114 KB | ✅ |
| **Heap libre max** | 171 KB | ✅ |
| **Reboots** | 2 | 🔴 |

---

## 🎯 PRIORITÉS DE CORRECTION

### **PRIORITÉ 1 - URGENT** 🔴
1. **Augmenter `AUTOMATION_TASK_STACK_SIZE` de 4096 à 8192 bytes minimum**
   - Fichier: `include/config.h:439`
   - Risque: Stack overflow dans automationTask
   - Impact: Crash système (Guru Meditation)

### **PRIORITÉ 2 - IMPORTANT** 🟡
2. **Vérifier et corriger la gestion du watchdog**
   - S'assurer que toutes les tâches critiques appellent `esp_task_wdt_reset()` régulièrement
   - Vérifier que `loopTask` ne reste pas bloquante > 3 secondes
   - Vérifier la gestion du watchdog dans les opérations réseau async

### **PRIORITÉ 3 - MOYEN** 🟢
3. **Corriger les messages d'erreur NVS** (si namespaces existent déjà, ne pas afficher d'erreur)
4. **Vérifier la configuration core dump** (désactiver en production ou ajouter partition)
5. **Vérifier le câblage/configuration OLED** (si nécessaire pour l'utilisateur)

---

## 📝 RECOMMANDATIONS GÉNÉRALES

1. **Monitoring continu**: Faire un monitoring de 90 secondes après chaque modification critique
2. **Test de stress**: Tester le système pendant plusieurs heures pour détecter les leaks mémoire
3. **Stack monitoring**: Activer le monitoring des High Water Marks (HWM) des stacks pour détecter les risques d'overflow
4. **Watchdog configuration**: Revoir la configuration du watchdog pour s'assurer qu'il est adapté aux tâches longues
5. **Documentation**: Documenter tous les changements de configuration dans `VERSION.md`

---

## ✅ ACTIONS À PRENDRE IMMÉDIATEMENT

1. [ ] Modifier `include/config.h` ligne 439: `AUTOMATION_TASK_STACK_SIZE = 8192`
2. [ ] Compiler et flasher le firmware
3. [ ] **Faire un nouveau monitoring de 90 secondes** pour vérifier que le stack overflow est résolu
4. [ ] Analyser les logs pour confirmer l'absence de Guru Meditation
5. [ ] Vérifier la HWM de la stack automationTask après modification

---

**Prochaine version recommandée**: v11.120 (correction stack overflow + watchdog)

---

*Généré automatiquement le 10 janvier 2026 à partir du monitoring v11.119*

