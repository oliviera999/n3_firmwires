# Rapport d'Analyse Exhaustive - Monitoring 3 minutes
**Date**: 26 janvier 2026  
**Version**: 11.157 (post-corrections stack overflow)  
**Fichier log**: monitor_wroom_test_2026-01-26_14-07-24.log  
**Durée**: ~3 minutes

## 📊 STATISTIQUES GÉNÉRALES

- **Lignes totales**: ~5400 lignes
- **Taille**: 603.54 KB
- **Période**: 14:07:26 - 14:10:11 (environ 2 minutes 45 secondes)

## 🔴 PROBLÈME CRITIQUE DÉTECTÉ

### **Reboots en boucle - PANIC reset**

Le système entre dans une **boucle de reboots** avec des crashes PANIC répétés :

1. **Nombre de reboots détectés**: 9 reboots en ~3 minutes
2. **Cause**: Reset reason 4 (PANIC reset - crash)
3. **RTC reason**: 12 (Watchdog Timeout)
4. **Intervalle moyen**: ~18-20 secondes entre chaque reboot

### Séquence observée :
```
14:07:33 → rst:0xc (SW_CPU_RESET), boot:0x13
14:07:35 → [BOOT] Reset reason: 4 - ⚠️ PANIC reset (crash)
14:07:54 → rst:0xc (SW_CPU_RESET), boot:0x13
14:07:56 → [BOOT] Reset reason: 4 - ⚠️ PANIC reset (crash)
14:08:12 → rst:0xc (SW_CPU_RESET), boot:0x13
14:08:14 → [BOOT] Reset reason: 4 - ⚠️ PANIC reset (crash)
... (pattern répété 9 fois)
```

## 🔍 ANALYSE DÉTAILLÉE

### 1. **CRASHES ET ERREURS CRITIQUES**

**Aucun crash "Stack canary" détecté dans ce log** ✅

Cependant, le système redémarre en boucle à cause de **Watchdog Timeout** (RTC code 12).

### 2. **DATAQUEUE ET ROTATION**

**Aucune activité DataQueue détectée** dans ce log.

Les corrections appliquées (réduction buffers, optimisation stack) n'ont **pas pu être testées** car le système ne fonctionne pas assez longtemps pour atteindre la limite de la queue.

### 3. **WATCHDOG ET TIMEOUTS**

**Problème majeur**: Watchdog Timeout (RTC code 12) détecté à chaque boot.

Le système crash avant que le watchdog puisse être correctement initialisé ou réinitialisé.

### 4. **SÉQUENCE DE DÉMARRAGE**

Le système démarre normalement :
- ✅ Initialisation capteurs (DHT22 désactivé - OK)
- ✅ Initialisation WaterTemp (16.8°C chargé depuis NVS)
- ✅ Initialisation servos (GPIO12, GPIO13)
- ✅ Initialisation WebSocket (port 81)
- ✅ Initialisation serveur web (port 80)
- ✅ Diagnostics chargés depuis NVS

**Mais crash avant la fin de l'initialisation complète** (~18-20 secondes après le boot).

### 5. **MÉMOIRE**

**État mémoire au démarrage**:
- Heap libre: ~140KB (après websocket)
- Heap libre: ~111KB (après web server)
- Fragmentation: 21-23%
- Largest block: 86-110 KB

**Pas d'alerte mémoire critique** avant les crashes.

### 6. **ERREURS NVS**

**Aucune erreur NVS critique** détectée.

Les sauvegardes de panic info fonctionnent correctement.

## 🎯 CAUSE IDENTIFIÉE

### **PROBLÈME CRITIQUE: Queues FreeRTOS non créées**

**Erreur détectée**: `assert failed: xQueueReceive queue.c:1531 (( pxQueue ))`

**Cause racine**: Les queues `g_sensorQueue` et `g_netQueue` sont déclarées mais **jamais créées** avec `xQueueCreate()`.

**Séquence du crash**:
1. Les tâches sont créées (`sensorTask`, `automationTask`, `netTask`)
2. Les tâches démarrent immédiatement
3. `automationTask` appelle `xQueueReceive(g_sensorQueue, ...)` à la ligne 301
4. `g_sensorQueue` est `nullptr` → **assert échoue** → crash
5. Le watchdog timeout se produit car le système est en panique

**Code problématique** (ligne 458 de `app_tasks.cpp`):
```cpp
// v11.157: Réorganisation ordre allocations - créer tâches AVANT queues
// ❌ ERREUR: Les queues doivent être créées AVANT les tâches qui les utilisent
```

**Correction appliquée**: Création des queues AVANT les tâches dans `AppTasks::start()`.

## 📋 POINTS POSITIFS

1. ✅ **Pas de crash "Stack canary"** - Les corrections stack overflow semblent avoir résolu ce problème
2. ✅ **Initialisation correcte** - Le système démarre correctement jusqu'à un certain point
3. ✅ **Mémoire stable** - Pas de problème de mémoire avant les crashes
4. ✅ **NVS fonctionnel** - Les sauvegardes fonctionnent

## ⚠️ PROBLÈMES IDENTIFIÉS

1. 🔴 **Boucle de reboots** - Le système ne peut pas fonctionner plus de 18-20 secondes
2. 🔴 **Watchdog Timeout** - Cause principale des crashes
3. ⚠️ **Impossible de tester DataQueue** - Le système crash avant d'atteindre la limite

## 🔧 CORRECTIONS APPLIQUÉES

### ✅ Correction 1: Création des queues FreeRTOS

**Fichier**: `src/app_tasks.cpp`

**Modification**: Ajout de la création des queues `g_sensorQueue` et `g_netQueue` **AVANT** la création des tâches.

```cpp
// Créer la queue capteurs (utilisée par sensorTask et automationTask)
g_sensorQueue = xQueueCreate(10, sizeof(SensorReadings));

// Créer la queue réseau (utilisée par netTask)
g_netQueue = xQueueCreate(5, sizeof(NetRequest*));
```

**Impact**: Les tâches peuvent maintenant utiliser les queues sans crash.

### Priorité 2: Tester les corrections DataQueue
Une fois la boucle de reboots résolue, tester spécifiquement :
- Remplir la queue jusqu'à 50 entrées
- Vérifier que la rotation fonctionne sans crash stack
- Vérifier le HWM de la stack de `loop()` après rotation

### Priorité 3: Monitoring approfondi
- Activer les logs de stack HWM pour toutes les tâches
- Ajouter des logs de watchdog reset
- Tracker le temps entre boot et crash

## 📝 CONCLUSION

**Statut**: 🔴 **SYSTÈME INSTABLE** (correction appliquée, re-test requis)

### Problème identifié
Le système crashait immédiatement après le démarrage des tâches à cause de **queues FreeRTOS non créées**. L'assert `xQueueReceive queue.c:1531 (( pxQueue ))` échouait car `g_sensorQueue` était `nullptr`.

### Corrections appliquées
1. ✅ **Création des queues avant les tâches** - `g_sensorQueue` et `g_netQueue` sont maintenant créées dans `AppTasks::start()`
2. ✅ **Optimisations DataQueue** - Réduction buffers et réutilisation du count (précédemment appliquées)

### Prochaines étapes
1. **Re-flasher** le firmware avec les corrections
2. **Re-monitorer** pendant 3 minutes pour valider que le crash est résolu
3. **Tester DataQueue** - Remplir la queue jusqu'à 50 entrées pour valider les optimisations stack
