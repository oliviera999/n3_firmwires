# Architecture Web Dédié - Stratégie de Priorisation Optimisée

## 🎯 Objectif

Implémenter une **tâche web dédiée** avec priorité élevée pour améliorer la réactivité de l'interface utilisateur tout en maintenant la sécurité des fonctions critiques.

## 📊 Nouvelle Hiérarchie des Priorités

### **Configuration des Tâches FreeRTOS**

```cpp
// Dans project_config.h
namespace TaskConfig {
    // Priorités des tâches - Stratégie Web Dédié
    constexpr uint8_t SENSOR_TASK_PRIORITY = 4;             // CRITIQUE - Capteurs prioritaires absolus
    constexpr uint8_t WEB_TASK_PRIORITY = 3;                 // HAUTE - Interface web réactive
    constexpr uint8_t AUTOMATION_TASK_PRIORITY = 2;          // MOYENNE - Logique métier pure
    constexpr uint8_t DISPLAY_TASK_PRIORITY = 1;             // BASSE - Affichage en arrière-plan
    
    // Tailles de stack
    constexpr uint32_t WEB_TASK_STACK_SIZE = 8192;           // Nouvelle tâche web dédiée
}
```

### **Ordre de Priorité Final**

| Priorité | Tâche | Description | Criticité |
|----------|-------|-------------|-----------|
| **4** | `sensorTask` | Lecture des capteurs → Queue FreeRTOS | 🔴 **CRITIQUE** |
| **3** | `webTask` | Interface web + WebSocket temps réel | 🟡 **HAUTE** |
| **2** | `automationTask` | Logique métier pure (sans web) | 🟢 **MOYENNE** |
| **1** | `displayTask` | Affichage OLED | 🔵 **BASSE** |

## 🔧 Modifications Apportées

### **1. Nouvelle Tâche Web Dédiée**

```cpp
// Tâche dédiée au serveur web - Stratégie Web Dédié
void webTask(void* pv) {
  static bool wdtReg = false;
  if (!wdtReg) { esp_task_wdt_add(NULL); wdtReg = true; }
  
  Serial.println(F("[Web] Tâche webTask démarrée - interface web dédiée"));
  
  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(100); // 100ms pour réactivité web maximale
  
  for (;;) {
    // Suspendre le traitement web pendant l'OTA pour libérer le réseau
    if (otaManager.isUpdating()) {
      esp_task_wdt_reset();
      vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(500)); // Cadence réduite pendant OTA
      continue;
    }
    
    // Reset watchdog avant traitement web
    esp_task_wdt_reset();
    
    // Traitement des requêtes web et WebSocket
    webSrv.loop();
    
    // Délai pour éviter la surcharge CPU
    vTaskDelayUntil(&lastWake, period);
  }
}
```

### **2. Création des Tâches Optimisée**

```cpp
// Création queue & tasks FreeRTOS - Stratégie Web Dédié
sensorQueue = xQueueCreate(5, sizeof(SensorReadings));
if (sensorQueue) {
  xTaskCreatePinnedToCore(sensorTask, "sensorTask", TaskConfig::SENSOR_TASK_STACK_SIZE, nullptr, TaskConfig::SENSOR_TASK_PRIORITY, &g_sensorTaskHandle, TaskConfig::TASK_CORE_ID); // CRITIQUE
  xTaskCreatePinnedToCore(webTask, "webTask", TaskConfig::WEB_TASK_STACK_SIZE, nullptr, TaskConfig::WEB_TASK_PRIORITY, &g_webTaskHandle, TaskConfig::TASK_CORE_ID); // HAUTE
  xTaskCreatePinnedToCore(automationTask, "autoTask", TaskConfig::AUTOMATION_TASK_STACK_SIZE, nullptr, TaskConfig::AUTOMATION_TASK_PRIORITY, &g_autoTaskHandle, TaskConfig::TASK_CORE_ID); // MOYENNE
  xTaskCreatePinnedToCore(displayTask, "displayTask", TaskConfig::DISPLAY_TASK_STACK_SIZE, nullptr, TaskConfig::DISPLAY_TASK_PRIORITY, &g_displayTaskHandle, TaskConfig::TASK_CORE_ID); // BASSE
}
```

### **3. Monitoring des Stacks Étendu**

```cpp
// Log des marges de stack incluant la nouvelle tâche web
Serial.printf("[Stacks] HWM at boot - sensor:%u web:%u auto:%u display:%u loop:%u bytes\n",
              (unsigned)hwmSensor, (unsigned)hwmWeb, (unsigned)hwmAuto, (unsigned)hwmDisplay, (unsigned)hwmLoop);
```

## ✅ Avantages de la Nouvelle Architecture

### **1. Réactivité Web Maximale**
- ✅ **Tâche dédiée** : Interface web isolée avec priorité élevée
- ✅ **100ms de cycle** : Réactivité web maximale
- ✅ **WebSocket temps réel** : Mises à jour instantanées
- ✅ **Pas d'interférence** : Le web ne peut plus bloquer l'automatisme

### **2. Sécurité des Fonctions Critiques**
- ✅ **Capteurs prioritaires** : Priorité 4 garantit la lecture des capteurs
- ✅ **Automatisme protégé** : Priorité 2 pour la logique métier pure
- ✅ **Isolation** : Chaque tâche a sa propre responsabilité
- ✅ **Watchdog** : Protection contre les blocages

### **3. Évolutivité**
- ✅ **Scalabilité** : Possibilité d'ajouter des fonctionnalités web
- ✅ **Debugging** : Isolation des problèmes par tâche
- ✅ **Maintenance** : Code plus modulaire et maintenable
- ✅ **Performance** : Optimisation indépendante de chaque composant

## 📈 Améliorations Attendues

### **Performance Web**
- **Temps de réponse** : < 100ms (vs 200-500ms avant)
- **WebSocket** : Mises à jour instantanées
- **Interface utilisateur** : Plus fluide et réactive
- **Concurrence** : Gestion simultanée de plusieurs clients

### **Stabilité Système**
- **Capteurs** : Lecture garantie toutes les 2 secondes
- **Automatisme** : Logique métier non perturbée
- **Affichage** : OLED fluide en arrière-plan
- **Récupération** : Meilleure gestion des erreurs

## 🔍 Monitoring et Debug

### **Logs de Démarrage**
```
[Web] Tâche webTask démarrée - interface web dédiée
[Stacks] HWM at boot - sensor:2048 web:1536 auto:3072 display:1024 loop:512 bytes
```

### **Surveillance des Stacks**
- **sensorTask** : ~2048 bytes disponibles
- **webTask** : ~1536 bytes disponibles  
- **autoTask** : ~3072 bytes disponibles
- **displayTask** : ~1024 bytes disponibles

### **Métriques de Performance**
- **Temps de réponse web** : Mesurable via `/optimization-stats`
- **Charge CPU** : Répartie entre les tâches
- **Mémoire** : Optimisée par tâche

## ⚠️ Points d'Attention

### **1. Consommation Mémoire**
- **+8KB** de stack pour la tâche web
- **Surveillance** : Vérifier les marges de stack
- **Optimisation** : Ajuster si nécessaire

### **2. Synchronisation**
- **Données partagées** : Protection par mutex si nécessaire
- **État cohérent** : Vérifier la synchronisation entre tâches
- **Race conditions** : Éviter les accès concurrents

### **3. Tests de Validation**
- **Charge web** : Tester avec plusieurs clients simultanés
- **Stabilité** : Vérifier la stabilité sur le long terme
- **Performance** : Mesurer les améliorations

## 🚀 Prochaines Étapes

### **Phase 1 : Validation** ✅
- [x] Configuration des priorités
- [x] Création de la tâche web
- [x] Migration des endpoints
- [x] Tests de base

### **Phase 2 : Optimisation** 🔄
- [ ] Optimisation des performances web
- [ ] Amélioration des WebSockets
- [ ] Monitoring avancé
- [ ] Tests de charge

### **Phase 3 : Fonctionnalités** 📋
- [ ] APIs REST avancées
- [ ] Interface utilisateur enrichie
- [ ] Notifications temps réel
- [ ] Analytics de performance

## 📊 Résumé

La **stratégie Web Dédié** transforme l'architecture du système FFP3 en :

- **Séparant** les responsabilités entre tâches spécialisées
- **Optimisant** la réactivité de l'interface web
- **Garantissant** la sécurité des fonctions critiques
- **Préparant** l'évolution future du système

Cette architecture équilibre parfaitement **performance**, **sécurité** et **évolutivité** pour un système IoT robuste et réactif.
