# 🔍 Analyse du Crash PANIC après 2h de fonctionnement - v11.125

**Date**: 2026-01-XX (11h15)  
**Version**: v11.125  
**Uptime avant crash**: ~2 heures  
**Compteur de redémarrages**: 17

## 📋 Informations du Crash

### Données du Mail de Boot
```
Raison du redémarrage: PANIC (erreur critique)
Compteur de redémarrages: 17
Dernière raison sauvegardée: PANIC
Heap libre au démarrage: 95592 bytes
Heap libre minimum historique: 52388 bytes
Modèle chip: ESP32-D0WD-V3
Révision chip: 301
Nombre de cores: 2
Fréquence CPU: 240 MHz
Version SDK: v5.5-1-gb66b5448e0
Requêtes HTTP réussies: 1338
Requêtes HTTP échouées: 1
Mises à jour OTA réussies: 0
Mises à jour OTA échouées: 0
```

### ⚠️ Problème Critique Identifié

**Les détails du PANIC ne sont PAS inclus dans le mail de boot**, alors que le système dispose d'un mécanisme de capture des détails Guru Meditation.

## 🔎 Analyse des Causes Probables

### 1. **Watchdog Timeout (CAUSE PRINCIPALE SUSPECTÉE)** 🔴

**Configuration actuelle** (`src/app.cpp:102`):
```cpp
cfg.timeout_ms = 120000;  // 120 secondes (2 minutes)
```

**Problème identifié**:
- Le timeout watchdog est configuré à **120 secondes** (2 minutes)
- La documentation indique qu'il devrait être à **300 secondes** (5 minutes)
- Après 2h de fonctionnement, une tâche peut avoir manqué de reset le watchdog à temps

**Scénario probable**:
1. Une tâche (probablement `sensorTask`, `automationTask`, ou `webTask`) effectue une opération longue
2. L'opération prend plus de 120 secondes sans reset du watchdog
3. Le watchdog déclenche un PANIC
4. Le système redémarre

**Indices**:
- 1338 requêtes HTTP réussies → activité réseau importante
- 1 requête HTTP échouée → possible timeout réseau qui a bloqué une tâche
- Crash après 2h → problème progressif ou opération longue occasionnelle

### 2. **Absence de Détails PANIC dans le Mail** 🟡

**Problème**:
Le mail de boot ne contient pas la section `⚠️ DÉTAILS DU PANIC (Guru Meditation) ⚠️` qui devrait être générée par `Diagnostics::generateRestartReport()`.

**Causes possibles**:
1. `capturePanicInfo()` ne capture pas correctement les infos depuis la mémoire RTC
2. Les infos sont capturées mais perdues avant la sauvegarde NVS
3. `loadPanicInfo()` ne charge pas les infos au bon moment
4. Les infos sont nettoyées trop tôt par `clearPanicInfo()`

**Code concerné** (`src/diagnostics.cpp:58-63`):
```cpp
// Charger les informations de panic sauvegardées du dernier boot
loadPanicInfo();

// Si c'était un panic, sauvegarder les nouvelles infos
if (esp_reset_reason() == ESP_RST_PANIC) {
  savePanicInfo();
}
```

**Problème potentiel**: `loadPanicInfo()` appelle `clearPanicInfo()` immédiatement après lecture (ligne 521), ce qui peut supprimer les infos avant qu'elles ne soient incluses dans le rapport.

### 3. **Fragmentation Mémoire Progressive** 🟡

**Indicateurs**:
- Heap libre au démarrage: **95592 bytes** (correct)
- Heap libre minimum historique: **52388 bytes** (correct, > 20KB seuil critique)
- Après 2h avec 1338 requêtes HTTP → nombreuses allocations/désallocations

**Risque**:
- Fragmentation mémoire progressive après de nombreuses allocations HTTP
- Une allocation importante (HTTPS, JSON parsing) peut échouer même si le heap total est suffisant
- Peut causer un crash silencieux ou un watchdog timeout si l'allocation bloque

### 4. **Stack Overflow Progressif** 🟡

**Risque**:
- Après 2h, une récursion profonde ou un appel de fonction avec gros paramètres peut causer un stack overflow
- Les tâches FreeRTOS ont des stacks limités (8192-12288 bytes selon la tâche)
- Un stack overflow peut causer un PANIC

### 5. **Deadlock ou Blocage de Tâche** 🟡

**Scénario**:
- Une tâche attend un mutex/sémaphore qui n'est jamais libéré
- La tâche ne reset pas le watchdog pendant l'attente
- Après 120 secondes, le watchdog déclenche un PANIC

## 🔧 Solutions Recommandées

### Solution 1: Augmenter le Timeout Watchdog (URGENT) 🔴

**Action**: Augmenter le timeout watchdog de 120s à 300s (5 minutes) comme indiqué dans la documentation.

**Fichier**: `src/app.cpp:102`

**Avant**:
```cpp
cfg.timeout_ms = 120000;  // 120 secondes (2 minutes)
```

**Après**:
```cpp
cfg.timeout_ms = 300000;  // 300 secondes (5 minutes) - conforme à la documentation
```

**Justification**:
- Les opérations HTTPS peuvent prendre jusqu'à 10-15 secondes
- Les lectures de capteurs peuvent prendre jusqu'à 2-3 secondes
- Avec plusieurs opérations enchaînées, 120s peut être insuffisant
- La documentation indique 300s comme valeur recommandée

### Solution 2: Corriger la Capture des Détails PANIC 🟡

**Problème**: Les détails PANIC sont nettoyés trop tôt.

**Fichier**: `src/diagnostics.cpp:505-523`

**Modification proposée**:
```cpp
// Charger les informations de panic depuis NVS
void Diagnostics::loadPanicInfo() {
  g_nvsManager.loadBool(NVS_NAMESPACES::LOGS, "diag_hasPanic", _stats.panicInfo.hasPanicInfo, false);
  
  if (_stats.panicInfo.hasPanicInfo) {
    g_nvsManager.loadString(NVS_NAMESPACES::LOGS, "diag_panicCause", _stats.panicInfo.exceptionCause, "Unknown");
    g_nvsManager.loadULong(NVS_NAMESPACES::LOGS, "diag_panicAddr", _stats.panicInfo.exceptionAddress, 0);
    g_nvsManager.loadULong(NVS_NAMESPACES::LOGS, "diag_panicExcv", _stats.panicInfo.excvaddr, 0);
    g_nvsManager.loadString(NVS_NAMESPACES::LOGS, "diag_panicTask", _stats.panicInfo.taskName, "");
    g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "diag_panicCore", _stats.panicInfo.core, -1);
    g_nvsManager.loadString(NVS_NAMESPACES::LOGS, "diag_panicInfo", _stats.panicInfo.additionalInfo, "");
    
    Serial.println(F("[Diagnostics] 📖 Informations de panic chargées depuis NVS"));
  }
  
  // NE PAS nettoyer immédiatement - laisser pour le rapport de boot
  // Le nettoyage sera fait après l'envoi du mail de boot
}
```

**Ajouter un appel à `clearPanicInfo()` après l'envoi du mail de boot** dans `src/app.cpp` après l'envoi du mail.

### Solution 3: Ajouter des Logs de Diagnostic Watchdog 🔵

**Action**: Ajouter des logs périodiques pour identifier quelle tâche cause le timeout.

**Fichier**: `src/app.cpp` dans chaque tâche

**Ajout proposé**:
```cpp
// Dans chaque tâche, ajouter un log périodique
static unsigned long lastWatchdogLog = 0;
if (millis() - lastWatchdogLog > 30000) {  // Toutes les 30 secondes
  Serial.printf("[%s] Watchdog reset OK, heap: %u, stack: %u\n", 
                pcTaskGetName(nullptr), 
                esp_get_free_heap_size(),
                uxTaskGetStackHighWaterMark(nullptr));
  lastWatchdogLog = millis();
}
esp_task_wdt_reset();
```

### Solution 4: Améliorer la Gestion Mémoire pour Requêtes HTTP 🔵

**Action**: Vérifier que toutes les allocations HTTP sont libérées correctement.

**Fichier**: `src/web_client.cpp`

**Vérifications**:
- Tous les buffers alloués sont libérés avec `free()` ou `delete`
- Les String Arduino sont évitées (utiliser `char[]` et `strncpy`)
- Les allocations sont vérifiées avant utilisation

### Solution 5: Monitoring Proactif 🔵

**Action**: Ajouter un monitoring proactif du heap et des stacks.

**Fichier**: `src/diagnostics.cpp`

**Ajout proposé**:
```cpp
void Diagnostics::checkSystemHealth() {
  uint32_t freeHeap = esp_get_free_heap_size();
  if (freeHeap < 30000) {  // Seuil d'alerte
    Serial.printf("[Diagnostics] ⚠️ ALERTE: Heap faible: %u bytes\n", freeHeap);
    // Optionnel: envoyer une alerte email
  }
  
  // Vérifier les stacks des tâches
  AppTasks::Handles handles = AppTasks::getHandles();
  if (handles.sensor) {
    UBaseType_t stack = uxTaskGetStackHighWaterMark(handles.sensor);
    if (stack < 1024) {  // Seuil d'alerte
      Serial.printf("[Diagnostics] ⚠️ ALERTE: Stack sensorTask faible: %u bytes\n", stack);
    }
  }
  // Répéter pour autres tâches...
}
```

## 📊 Priorisation des Actions

### 🔴 URGENT (À faire immédiatement)
1. **Augmenter le timeout watchdog à 300s** (Solution 1)
2. **Corriger la capture des détails PANIC** (Solution 2)

### 🟡 IMPORTANT (À faire rapidement)
3. Ajouter des logs de diagnostic watchdog (Solution 3)
4. Vérifier la gestion mémoire HTTP (Solution 4)

### 🔵 AMÉLIORATION (À planifier)
5. Monitoring proactif (Solution 5)

## 🧪 Tests Recommandés

1. **Test de stabilité longue durée**:
   - Lancer un monitoring de 3-4 heures
   - Vérifier l'absence de crash PANIC
   - Monitorer le heap minimum et les stacks

2. **Test de charge HTTP**:
   - Simuler 1500+ requêtes HTTP
   - Vérifier l'absence de fragmentation mémoire
   - Monitorer les timeouts watchdog

3. **Test de récupération après crash**:
   - Vérifier que les détails PANIC sont bien capturés et inclus dans le mail de boot

## 📝 Notes Additionnelles

- **17 redémarrages**: Indique un problème récurrent. Il est important d'identifier la cause racine.
- **1 requête HTTP échouée**: Peut être liée au crash (timeout réseau qui a bloqué une tâche).
- **Heap minimum 52388 bytes**: Correct, mais après 2h, la fragmentation peut rendre certaines allocations impossibles même avec un heap total suffisant.

## 🔗 Références

- Documentation watchdog: `docs/fixes/WATCHDOG_TIMEOUT_FIX.md`
- Capture PANIC: `docs/guides/PANIC_GURU_MEDITATION_DETAILS.md`
- Configuration watchdog: `src/app.cpp:100-106`
- Capture panic: `src/diagnostics.cpp:382-487`

---

**Prochaine étape**: Implémenter les solutions URGENT (1 et 2) et tester la stabilité.
