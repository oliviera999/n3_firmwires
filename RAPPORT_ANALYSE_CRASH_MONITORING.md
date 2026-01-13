# 🔍 Rapport d'Analyse - Monitoring jusqu'au Crash

**Date:** 12 janvier 2026  
**Version:** v11.124  
**Durée du monitoring:** 62.1 secondes  
**Fichiers analysés:** 
- `monitor_until_crash_2026-01-12_15-47-43.log` (2995 lignes)
- `monitor_until_crash_analysis_2026-01-12_15-47-43.txt`

---

## 📊 Résumé Exécutif

Le monitoring a capturé **6 redémarrages consécutifs** (reboot #124 à #129) en seulement 62 secondes, indiquant un problème de stabilité critique. Cependant, **aucun crash PANIC explicite** n'a été détecté dans les logs, ce qui suggère des redémarrages "silencieux" ou des problèmes non loggés.

### Problèmes Critiques Identifiés

1. **Redémarrages fréquents** : 6 redémarrages en 62 secondes
2. **Heap minimum faible** : 52388 bytes (51 KB) - proche de la limite critique
3. **Aucun log de cause de redémarrage** : Les redémarrages ne sont pas précédés de messages d'erreur explicites

---

## 🔴 Problème Principal : Redémarrages en Boucle

### Observations

```
Reboot #124 → #125 → #126 → #127 → #128 → #129
Temps entre redémarrages : ~10-15 secondes
Heap minimum constant : 52388 bytes
```

### Causes Probables

#### 1. **Watchdog Timeout Non Loggé** ⚠️ CRITIQUE
- Le watchdog est configuré à **60 secondes** (`app.cpp:79`)
- Les redémarrages se produisent toutes les **10-15 secondes**
- **Hypothèse** : Une tâche ne reset pas le watchdog à temps, mais l'erreur n'est pas loggée avant le redémarrage

**Vérification nécessaire :**
```cpp
// Dans app.cpp ligne 79
cfg.timeout_ms = 60000;  // 60 secondes
```

#### 2. **Problème de Mémoire (Heap Fragmentation)** ⚠️ ATTENTION
- Heap minimum : **52388 bytes** (51 KB)
- Seuil critique : **< 50000 bytes** (50 KB)
- Le système est **très proche de la limite critique**

**Impact :**
- Allocations échouent silencieusement
- Fragmentation mémoire élevée
- Risque de crash lors d'allocations importantes (HTTPS, JSON)

#### 3. **Redémarrage Silencieux (Brownout ou Hardware)** ⚠️ POSSIBLE
- Aucun message "Brownout" dans les logs
- Mais les redémarrages sont trop réguliers pour être aléatoires
- **Hypothèse** : Problème d'alimentation ou de hardware non détecté

---

## 🔍 Analyse Détaillée des Logs

### 1. Patterns de Redémarrage

Chaque redémarrage suit ce pattern :
```
rst:0x1 (POWERON_RESET)
→ Initialisation système
→ Chargement NVS
→ Connexion WiFi
→ Démarrage tâches FreeRTOS
→ Redémarrage après ~10-15 secondes
```

### 2. État de la Mémoire

```
Heap libre au démarrage : ~237 KB
Heap libre minimum : 52 KB (CRITIQUE)
Heap libre actuel : ~96 KB (variable)
```

**Problème :** Le heap minimum est très faible et constant, indiquant une fuite mémoire ou une fragmentation importante.

### 3. Tâches FreeRTOS

Les tâches sont correctement créées :
- `sensorTask` : Stack 4096 bytes
- `webTask` : Stack 6144 bytes  
- `automationTask` : Stack 12288 bytes
- `displayTask` : Stack 4096 bytes

**Observation :** Toutes les tâches enregistrent le watchdog correctement (`esp_task_wdt_add(nullptr)`).

### 4. Opérations Réseau

Les requêtes HTTPS fonctionnent correctement :
- Connexions SSL/TLS réussies
- Heap avant TLS : ~96 KB
- Heap après TLS : ~53 KB (chute importante !)

**Problème identifié :** Les opérations HTTPS consomment **~43 KB de heap**, ce qui peut déclencher un crash si le heap est déjà faible.

---

## 💡 Solutions Proposées

### Solution 1 : Augmenter le Timeout Watchdog (URGENT)

**Problème :** Le timeout de 60 secondes peut être trop court si une opération réseau bloque.

**Solution :**
```cpp
// Dans src/app.cpp ligne 79
cfg.timeout_ms = 120000;  // Augmenter à 120 secondes (2 minutes)
```

**Justification :** Les opérations HTTPS peuvent prendre jusqu'à 5-10 secondes, et si plusieurs opérations s'enchaînent, le watchdog peut expirer.

---

### Solution 2 : Ajouter des Logs Avant Redémarrage (DIAGNOSTIC)

**Problème :** On ne sait pas pourquoi le système redémarre.

**Solution :** Ajouter un handler de redémarrage qui log la cause :

```cpp
// Dans src/app.cpp, après setup()
void printResetReason() {
  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("[BOOT] Reset reason: %d\n", reason);
  
  switch(reason) {
    case ESP_RST_POWERON: Serial.println("[BOOT] Power-on reset"); break;
    case ESP_RST_EXT: Serial.println("[BOOT] External reset"); break;
    case ESP_RST_SW: Serial.println("[BOOT] Software reset"); break;
    case ESP_RST_PANIC: Serial.println("[BOOT] PANIC reset"); break;
    case ESP_RST_INT_WDT: Serial.println("[BOOT] Interrupt watchdog"); break;
    case ESP_RST_TASK_WDT: Serial.println("[BOOT] Task watchdog"); break;
    case ESP_RST_WDT: Serial.println("[BOOT] Other watchdog"); break;
    case ESP_RST_DEEPSLEEP: Serial.println("[BOOT] Deep sleep"); break;
    case ESP_RST_BROWNOUT: Serial.println("[BOOT] Brownout"); break;
    case ESP_RST_SDIO: Serial.println("[BOOT] SDIO"); break;
    default: Serial.println("[BOOT] Unknown reset"); break;
  }
}
```

---

### Solution 3 : Vérifier et Réduire la Consommation Mémoire (CRITIQUE)

**Problème :** Heap minimum de 52 KB est trop faible.

**Solutions :**

#### 3.1. Vérifier les Fuites Mémoire
```cpp
// Ajouter dans automationTask, avant chaque opération importante
uint32_t heapBefore = esp_get_free_heap_size();
// ... opération ...
uint32_t heapAfter = esp_get_free_heap_size();
if (heapAfter < heapBefore - 1000) {  // Perte > 1KB
  Serial.printf("[MEM] ⚠️ Fuite mémoire détectée: %d bytes\n", heapBefore - heapAfter);
}
```

#### 3.2. Réduire la Taille des Buffers JSON
```cpp
// Dans automatism_network.cpp
// Réduire BufferConfig::JSON_DOCUMENT_SIZE si possible
```

#### 3.3. Libérer la Mémoire Après les Opérations HTTPS
```cpp
// Après chaque requête HTTPS, forcer un garbage collection
// (si possible avec ArduinoJson)
```

---

### Solution 4 : Ajouter des Guards de Mémoire Avant les Opérations Critiques

**Problème :** Les opérations HTTPS consomment beaucoup de mémoire.

**Solution :**
```cpp
// Dans web_client.cpp, avant chaque requête HTTPS
uint32_t freeHeap = esp_get_free_heap_size();
if (freeHeap < 70000) {  // Seuil minimum de 70 KB
  Serial.printf("[HTTP] ⚠️ Heap trop faible (%u bytes), report de la requête\n", freeHeap);
  return false;  // Reporter la requête
}
```

---

### Solution 5 : Améliorer le Monitoring du Watchdog

**Problème :** On ne sait pas quelle tâche cause le timeout watchdog.

**Solution :**
```cpp
// Dans chaque tâche, ajouter un log périodique
void automationTask(void* pv) {
  unsigned long lastWdtLog = 0;
  for (;;) {
    esp_task_wdt_reset();
    
    // Log toutes les 30 secondes
    if (millis() - lastWdtLog > 30000) {
      Serial.printf("[autoTask] Watchdog reset OK, heap: %u\n", esp_get_free_heap_size());
      lastWdtLog = millis();
    }
    
    // ... reste du code ...
  }
}
```

---

## 🎯 Plan d'Action Prioritaire

### Phase 1 : Diagnostic Immédiat (URGENT)
1. ✅ **Ajouter les logs de reset reason** (Solution 2)
2. ✅ **Augmenter le timeout watchdog à 120s** (Solution 1)
3. ✅ **Ajouter des guards mémoire avant HTTPS** (Solution 4)

### Phase 2 : Correction Mémoire (CRITIQUE)
1. ✅ **Identifier les fuites mémoire** (Solution 3.1)
2. ✅ **Réduire la consommation mémoire HTTPS** (Solution 3.2)
3. ✅ **Optimiser les buffers JSON** (Solution 3.3)

### Phase 3 : Monitoring Amélioré
1. ✅ **Ajouter des logs watchdog périodiques** (Solution 5)
2. ✅ **Monitorer le heap en continu**
3. ✅ **Alertes si heap < 60 KB**

---

## 📝 Recommandations Finales

1. **URGENT** : Augmenter le timeout watchdog et ajouter les logs de reset reason
2. **CRITIQUE** : Résoudre le problème de mémoire (heap minimum trop faible)
3. **IMPORTANT** : Ajouter des guards mémoire avant les opérations critiques
4. **RECOMMANDÉ** : Améliorer le monitoring pour identifier la cause exacte des redémarrages

---

## 🔄 Prochaines Étapes

1. Implémenter les solutions de la Phase 1
2. Relancer le monitoring jusqu'au crash
3. Analyser les nouveaux logs avec les informations de reset reason
4. Identifier la tâche ou l'opération qui cause les redémarrages
5. Appliquer les corrections de la Phase 2

---

**Note :** Ce rapport est basé sur une analyse de 62 secondes de monitoring. Un monitoring plus long (plusieurs heures) serait nécessaire pour identifier des patterns à long terme, mais les redémarrages fréquents observés indiquent déjà un problème critique qui doit être résolu en priorité.
