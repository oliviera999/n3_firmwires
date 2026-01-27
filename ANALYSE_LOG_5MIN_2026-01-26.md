# Analyse du Log de Monitoring - 5 minutes
**Date**: 26 janvier 2026  
**Fichier**: `monitor_wroom_test_2026-01-26_19-51-42.log`  
**Durée**: ~1 minute 12 secondes (monitoring interrompu)

---

## 🔴 PROBLÈME CRITIQUE IDENTIFIÉ

### **Stack Overflow (Stack Canary Watchpoint Triggered)**

Le système entre en **boucle de crashes** à cause de débordements de pile (stack overflow).

**Erreur principale**:
```
Guru Meditation Error: Core  1 panic'ed (Unhandled debug exception). 
Debug exception reason: Stack canary watchpoint triggered
```

**Statistiques**:
- **Crashes détectés**: 5 crashes PANIC
- **Reboots**: 4 reboots automatiques
- **Pattern**: Crash → Reboot → Crash (boucle)

---

## 📊 STATISTIQUES GÉNÉRALES

- **Lignes totales**: 4959 lignes
- **Taille**: ~320 KB
- **WiFi connexions**: 4 connexions réussies
- **Erreurs HTTP**: 0 (pas d'échange réseau avant crash)
- **Durée avant premier crash**: ~8 secondes après boot

---

## 🔍 ANALYSE DÉTAILLÉE

### Séquence des Crashes

1. **Premier crash** (19:51:50.987)
   - Stack canary watchpoint triggered
   - Backtrace corrompu
   - Re-entered core dump

2. **Reboot** (19:51:52.975)
   - Reset reason: PANIC reset (crash)
   - Boot normal

3. **Deuxième crash** (19:52:15.108)
   - Même erreur: Stack canary watchpoint triggered
   - Intervalle: ~22 secondes après boot

4. **Troisième crash** (19:52:41.695)
   - Même erreur
   - Intervalle: ~24 secondes après boot

5. **Quatrième crash** (19:53:05.854)
   - Même erreur
   - Intervalle: ~18 secondes après boot

### Observations

- **Pattern répétitif**: Tous les crashes sont identiques (Stack canary)
- **Timing**: Crashes entre 18-24 secondes après chaque boot
- **Backtrace**: Toujours corrompu (`|<-CORRUPTED`)
- **Pas d'échange réseau**: Aucune requête HTTP avant les crashes

---

## ✅ POINTS POSITIFS

1. **Boot réussi**: Le système démarre correctement
2. **WiFi fonctionnel**: Connexions WiFi réussies (4 fois)
3. **Configuration NVS**: Chargement correct
4. **Capteurs**: Lecture des capteurs fonctionnelle
5. **Pas d'erreurs HTTP**: Aucune erreur réseau détectée

---

## ⚠️ CAUSES PROBABLES

### 1. Stack Overflow dans loopTask (Probable)

Le crash se produit dans `loopTask` (tâche principale Arduino). Les causes possibles:

- **Stack trop petite**: La stack de `loopTask` pourrait être insuffisante
- **Allocation locale excessive**: Variables locales trop grandes dans `loop()`
- **Récursion**: Appels récursifs non contrôlés
- **Buffer overflow**: Dépassement de buffer local

### 2. Stack Overflow dans une autre tâche

Bien que moins probable, une autre tâche pourrait causer le problème:
- `sensorTask`
- `automationTask`
- `netTask`
- `displayTask`
- `webTask`

---

## 🔧 RECOMMANDATIONS

### 1. Augmenter la Stack de loopTask (URGENT)

Dans `platformio.ini`, la stack de `loopTask` est configurée à 16384 bytes:
```ini
-DCONFIG_ARDUINO_LOOP_STACK_SIZE=16384
```

**Action**: Augmenter à 20480 ou 24576 bytes

### 2. Vérifier les Allocations Locales

Examiner `app.cpp::loop()` pour:
- Variables locales de grande taille
- Buffers statiques dans la fonction
- Appels récursifs

### 3. Analyser les Stacks des Tâches

Vérifier les High Water Marks (HWM) de toutes les tâches:
- `sensorTask`
- `automationTask`
- `netTask`
- `displayTask`
- `webTask`
- `loopTask`

### 4. Activer les Logs de Stack

Vérifier si les logs montrent des HWM faibles avant les crashes.

---

## 📝 PROCHAINES ÉTAPES

1. **URGENT**: Augmenter `CONFIG_ARDUINO_LOOP_STACK_SIZE` à 20480 ou 24576
2. Analyser `app.cpp::loop()` pour allocations locales
3. Vérifier les HWM de toutes les tâches
4. Recompiler et tester
5. Si le problème persiste, analyser le core dump

---

**Conclusion**: Le firmware fonctionne correctement mais crash à cause d'un stack overflow dans `loopTask`. Augmenter la stack devrait résoudre le problème.
