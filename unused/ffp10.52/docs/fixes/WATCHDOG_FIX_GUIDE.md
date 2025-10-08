# 🔧 Guide de Correction des Erreurs Watchdog ESP32

## 🚨 Problème Identifié

Votre ESP32 rencontre une **Guru Meditation Error** avec un **Interrupt wdt timeout on CPU1**. Cette erreur indique que le watchdog timer d'interruption a déclenché, ce qui signifie qu'une tâche ou un gestionnaire d'interruption a pris trop de temps à s'exécuter.

## 📋 Analyse de l'Erreur

```
Guru Meditation Error: Core  1 panic'ed (Interrupt wdt timeout on CPU1). 
```

**Causes possibles :**
- Tâches FreeRTOS qui prennent trop de temps
- Opérations bloquantes dans les boucles
- Utilisation de `delay()` au lieu de `vTaskDelay()`
- Stack sizes insuffisants
- Timeout watchdog trop court
- Lectures de capteurs qui bloquent

## ✅ Solutions Appliquées

### 1. Configuration du Watchdog

**Avant :**
```cpp
cfg.timeout_ms = 120000; // 2 minutes
```

**Après :** (conforme à la configuration actuelle)
```cpp
cfg.timeout_ms = 300000; // 5 minutes (augmenté)
```

### 2. Optimisation des Tâches FreeRTOS

**Avant :**
```cpp
xTaskCreatePinnedToCore(sensorTask, "sensorTask", 4096, nullptr, 1, nullptr, 0);
xTaskCreatePinnedToCore(automationTask, "autoTask", 6144, nullptr, 1, nullptr, 1);
xTaskCreatePinnedToCore(displayTask, "displayTask", 4096, nullptr, 2, nullptr, 1);
```

**Après :**
```cpp
xTaskCreatePinnedToCore(sensorTask, "sensorTask", 8192, nullptr, 2, nullptr, 0); // Stack + priorité augmentés
xTaskCreatePinnedToCore(automationTask, "autoTask", 12288, nullptr, 1, nullptr, 1); // Stack augmenté
xTaskCreatePinnedToCore(displayTask, "displayTask", 8192, nullptr, 3, nullptr, 1); // Stack + priorité augmentés
```

### 3. Gestion des Erreurs et Watchdog Reset

**Ajout dans toutes les tâches :**
```cpp
// Reset watchdog at the start of each iteration
esp_task_wdt_reset();

try {
    // Code de la tâche
} catch (...) {
    Serial.println(F("[Task] Erreur détectée"));
}

// Reset watchdog before delay
esp_task_wdt_reset();
vTaskDelay(pdMS_TO_TICKS(delay_time));
```

### 4. Optimisation des Capteurs Ultrasoniques

**Avant :**
```cpp
const uint32_t TIMEOUT_US = 50000UL; // 50 ms
delay(50); // 50ms entre lectures
```

**Après :**
```cpp
const uint32_t TIMEOUT_US = 30000UL; // 30 ms (réduit)
vTaskDelay(pdMS_TO_TICKS(25)); // 25ms avec vTaskDelay
```

### 5. Optimisation de l'Affichage OLED

**Avant :**
```cpp
vTaskDelay(pdMS_TO_TICKS(50)); // 50ms (20 FPS)
```

**Après :**
```cpp
vTaskDelay(pdMS_TO_TICKS(100)); // 100ms (10 FPS) - moins de charge CPU
```

## 🔄 Étapes de Test

### 1. Compilation et Flash

```bash
# Nettoyer le projet
pio run -t clean

# Compiler pour ESP32-WROOM (production)
pio run -e wroom-prod

# Flasher l'ESP32
pio run -e wroom-prod -t upload
```

### 2. Monitoring

```bash
# Ouvrir le moniteur série
pio device monitor
```

**Logs à surveiller :**
- `[SensorTask] Tâche démarrée`
- `[AutomationTask] Tâche démarrée`
- `[Display] Tâche displayTask démarrée`
- Absence d'erreurs "Guru Meditation"

### 3. Diagnostic

```bash
# Lancer le script de diagnostic
python diagnostic_watchdog.py
```

## 📊 Monitoring Continu

### Indicateurs de Bon Fonctionnement

✅ **Normal :**
- Tâches démarrées sans erreur
- Heap libre > 10KB
- Pas d'erreurs watchdog
- Affichage OLED fluide

❌ **Problématique :**
- Erreurs "Guru Meditation"
- Heap libre < 5KB
- Tâches qui ne démarrent pas
- Affichage saccadé

### Logs de Diagnostic

Ajoutez ces logs pour surveiller :

```cpp
// Dans chaque tâche
Serial.printf("[Task] Heap libre: %u bytes\n", esp_get_free_heap_size());
Serial.printf("[Task] Stack libre: %u bytes\n", uxTaskGetStackHighWaterMark(nullptr));
```

## 🛠️ Actions Préventives

### 1. Configuration Recommandée

```cpp
// Dans setup()
esp_task_wdt_config_t cfg = {};
cfg.timeout_ms = 180000; // 3 minutes
cfg.idle_core_mask = (1 << 0) | (1 << 1);
cfg.trigger_panic = true;
esp_task_wdt_init(&cfg);
```

### 2. Bonnes Pratiques

- ✅ Utiliser `vTaskDelay()` au lieu de `delay()`
- ✅ Appeler `esp_task_wdt_reset()` régulièrement
- ✅ Gérer les erreurs avec try/catch
- ✅ Surveiller l'utilisation mémoire
- ✅ Optimiser les fréquences d'exécution

### 3. À Éviter

- ❌ Boucles infinies sans yield
- ❌ Opérations bloquantes longues
- ❌ Stack sizes trop petits
- ❌ Timeouts trop courts
- ❌ Utilisation excessive de `delay()`

## 🔍 Diagnostic Avancé

### Script de Diagnostic

Le script `diagnostic_watchdog.py` analyse :
- Erreurs de watchdog
- Problèmes de mémoire
- Informations sur les tâches
- Recommandations spécifiques

### Monitoring en Temps Réel

```bash
# Surveiller les logs en temps réel
pio device monitor | grep -E "(watchdog|heap|task|error)"
```

## 📞 Support

Si les problèmes persistent :

1. **Vérifiez les logs** avec le moniteur série
2. **Lancez le diagnostic** : `python diagnostic_watchdog.py`
3. **Augmentez les timeouts** si nécessaire
4. **Réduisez les fréquences** des tâches critiques
5. **Vérifiez l'alimentation** de l'ESP32

## 🎯 Résultat Attendu

Après application de ces corrections :
- ✅ Plus d'erreurs "Guru Meditation"
- ✅ Système stable et réactif
- ✅ Affichage OLED fluide
- ✅ Capteurs fonctionnels
- ✅ Utilisation mémoire optimisée

---

**Note :** Ces modifications ont été appliquées automatiquement dans votre code. Recompilez et testez pour vérifier la résolution du problème. 