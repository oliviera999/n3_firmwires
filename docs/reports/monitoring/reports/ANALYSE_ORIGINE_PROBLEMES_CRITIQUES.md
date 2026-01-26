# Analyse de l'Origine des Problèmes Critiques

**Date:** 21 janvier 2026  
**Firmware:** v11.156  
**Basé sur:** Monitoring 15 minutes (4 min 48 sec effectifs)  
**Fichier log:** `monitor_15min_v11.156_2026-01-21_22-29-41.log`

---

## Résumé Exécutif

Cette analyse documente l'origine des 4 problèmes critiques identifiés lors du monitoring :
1. **Redémarrages en boucle** (281 occurrences)
2. **Panic** (6 occurrences - "Unknown Exception")
3. **Mémoire critique** (Heap minimum: 25,300 bytes)
4. **Queue pleine** (19 erreurs)

**Conclusion principale:** Le capteur DHT22 défaillant est le déclencheur d'une cascade de défaillances qui provoque l'instabilité du système.

---

## 1. REDÉMARRAGES EN BOUCLE (281 occurrences)

### Cause racine : Cascade de défaillances

**Mécanisme identifié :**

#### 1.1 Watchdog Timeout (5 minutes)

**Fichier:** `src/app.cpp:109`

```cpp
cfg.timeout_ms = 300000;  // 300 secondes (5 minutes)
cfg.trigger_panic = true;
```

- Si aucune tâche ne reset le watchdog dans les 5 minutes → redémarrage automatique
- Le watchdog est reset dans `sensorTask` (`src/app_tasks.cpp:182, 195`) mais **pas pendant les blocages DHT22**

#### 1.2 Blocages DHT22

**Fichier:** `src/sensors.cpp:842`

```cpp
// Note: _dht.readTemperature() a son propre timeout interne, 
// mais peut prendre jusqu'à 7-8s si capteur défaillant
float temp = _dht.readTemperature();
```

- Le capteur DHT22 défaillant peut bloquer jusqu'à **7-8 secondes**
- Pendant ce blocage, le watchdog n'est **pas reset**
- Si plusieurs blocages s'accumulent (ex: 40+ blocages de 7s chacun), le timeout de 5 minutes peut être atteint

**Séquence observée:**
- Blocage DHT22: 7-8 secondes
- Pendant ce temps: watchdog non reset
- Si 40+ blocages: 40 × 7s = 280 secondes → proche du timeout de 300 secondes

#### 1.3 Boucle de redémarrage

**Cascade identifiée:**

```
1. Système démarre
2. Initialisation consomme mémoire (heap passe à ~25 KB)
3. Tentative opération TLS (requiert 52 KB)
4. Échec allocation → Panic
5. Redémarrage automatique
6. Retour à l'étape 1 (boucle infinie)
```

**Pourquoi 281 redémarrages en 4 min 48 sec ?**

- Durée moyenne par cycle : ~1 seconde (démarrage + initialisation + panic)
- 4 min 48 sec = 288 secondes
- 288 secondes / ~1 seconde = ~288 cycles
- Cohérent avec **281 redémarrages détectés**

**Preuve dans les logs:**
- Reset reason: 6 (ESP reset)
- RTC reason: 12 (watchdog timeout probable)
- Panic précédent: "Unknown Exception"

---

## 2. PANIC (6 occurrences - "Unknown Exception")

### Cause racine : Code RTC reason non reconnu

**Fichier:** `src/diagnostics.cpp:814-819`

```cpp
default:
  // Pour les codes inconnus, on affiche le code numérique
  exceptionCauseStr = "Unknown Exception";
  snprintf(_stats.panicInfo.additionalInfo, sizeof(_stats.panicInfo.additionalInfo),
           " (Reset code: %d)", (int)rtcReason);
  break;
```

### Causes probables

#### 2.1 Watchdog Timeout (RTC reason = 12)

- Le watchdog déclenche un reset avec code RTC non standard
- Le code ne reconnaît pas ce code → "Unknown Exception"
- **Observé dans les logs:** "Reset code: 12"

**Codes RTC reconnus dans le code:**
- `RTCWDT_RTC_RESET`
- `TG0WDT_SYS_RESET`
- `TG1WDT_SYS_RESET`
- `RTCWDT_CPU_RESET`
- Etc.

**Code 12 non reconnu** → tombe dans le `default` → "Unknown Exception"

#### 2.2 Allocation mémoire échouée

- Tentative d'allocation TLS avec heap < 52 KB
- mbedTLS/ESP-IDF génère un panic avec code RTC non standard
- Le code ne reconnaît pas ce code → "Unknown Exception"

**Séquence:**
1. Heap minimum = 25,300 bytes (< 52 KB requis)
2. Tentative allocation TLS → échec
3. mbedTLS génère panic avec code RTC non standard
4. Code ne reconnaît pas → "Unknown Exception"

#### 2.3 Stack overflow

- Une tâche dépasse sa stack
- Génère un panic avec code RTC non reconnu
- Code ne reconnaît pas → "Unknown Exception"

### Pourquoi seulement 6 panics détectés sur 281 redémarrages ?

**Les panics ne sont capturés que si :**

1. Le reset reason est `ESP_RST_PANIC`, `ESP_RST_INT_WDT`, ou `ESP_RST_TASK_WDT`
   - Vérification: `src/diagnostics.cpp:753-756`

2. Le système a le temps d'écrire dans NVS avant le redémarrage
   - Si le redémarrage est trop rapide, l'écriture NVS échoue

**Explication:**
- La plupart des redémarrages sont probablement des **watchdog timeouts** qui ne sont pas tous capturés comme panics
- Seulement 6 panics ont eu le temps d'être écrits dans NVS avant le redémarrage suivant

---

## 3. MÉMOIRE CRITIQUE (Heap minimum: 25,300 bytes)

### Cause racine : Fragmentation mémoire

**Seuil TLS:** `include/tls_mutex.h:26`

```cpp
constexpr uint32_t TLS_MIN_HEAP_BYTES = 52000; // 52 KB requis
```

- **Heap observé:** 25,300 bytes (moins de la moitié requis)
- **Heap libre au moment de l'alerte:** 75,452 bytes
- **Problème:** Fragmentation - même avec 75 KB libre, le plus grand bloc contigu est seulement 25 KB

### Causes de la fragmentation

#### 3.1 Allocations statiques importantes

**Identifiées dans le code:**

- **Buffers WebSocket:** Allocation dynamique mais persistante
- **Buffers HTTP:** Allocation/désallocation fréquentes
- **Historiques de capteurs:**
  - DHT22: Historique de température/humidité
  - Température eau: Historique
- **Queues FreeRTOS:**
  - Queue capteurs: 25 slots × sizeof(SensorReadings)
  - Queue réseau: 6 slots × sizeof(NetRequest*)

**Fichiers concernés:**
- `src/app_tasks.cpp:451` - Queue capteurs (25 slots)
- `src/app_tasks.cpp:461` - Queue réseau (6 slots)
- `src/sensors.cpp` - Historiques DHT22 et température eau

#### 3.2 Fragmentation par allocations/désallocations

**String Arduino (allocation dynamique):**
- ~442 occurrences dans le code (grep effectué)
- Chaque String alloue/désalloue de la mémoire
- Fragmente le heap au fil du temps

**Buffers temporaires:**
- Allocation pour requêtes HTTP
- Allocation pour parsing JSON
- Allocation pour opérations TLS

**Opérations réseau:**
- Allocation/désallocation fréquentes lors des requêtes HTTP/HTTPS
- Chaque requête alloue des buffers temporaires

#### 3.3 Pas de défragmentation automatique

- ESP32 ne défragmente pas automatiquement le heap
- Le heap minimum reste bas même si le heap libre est élevé
- **Exemple observé:** 75 KB libre mais minimum = 25 KB (fragmentation)

### Pourquoi 25,300 bytes spécifiquement ?

**Fragmentation stabilisée:**
- Après plusieurs cycles d'allocation/désallocation, la fragmentation se stabilise
- Le plus grand bloc contigu disponible devient constant
- **Taille minimale de bloc libre:** ~25 KB

**Mécanisme:**
1. Allocations initiales créent des "trous" dans le heap
2. Désallocations laissent des espaces libres fragmentés
3. Nouvelles allocations ne peuvent pas utiliser ces espaces (trop petits)
4. Le plus grand bloc contigu disponible se stabilise à ~25 KB

**Vérification dans le code:** `src/app.cpp:257-259`

```cpp
if (heapMin < TLS_MIN_HEAP_BYTES) {
  Serial.printf("[loop] 🔴 CRITICAL: Heap minimum critique: %u bytes (< %u KB requis pour TLS)\n", 
                heapMin, TLS_MIN_HEAP_BYTES / 1024);
}
```

**Observé dans les logs:**
```
[loop] 🔴 CRITICAL: Heap minimum critique: 25300 bytes (< 50 KB requis pour TLS)
```

---

## 5. QUEUE PLEINE (19 erreurs)

### Cause racine : Queue saturée par blocages DHT22

**Configuration:** `src/app_tasks.cpp:451`

```cpp
// v11.156: Augmenté de 10 à 25 slots pour éviter saturation
g_sensorQueue = xQueueCreate(25, sizeof(SensorReadings));
```

- Queue configurée à **25 slots** (augmentée de 10 à 25 dans v11.156)
- Même avec 25 slots, la queue se remplit

### Mécanisme de saturation

#### Séquence d'événements:

1. **Tâche sensorTask** lit les capteurs toutes les 500ms
   - Fichier: `src/app_tasks.cpp:196` - `vTaskDelay(pdMS_TO_TICKS(500))`

2. **DHT22 bloque** pendant 5-8 secondes (timeout)
   - Fichier: `src/sensors.cpp:842` - `_dht.readTemperature()` peut bloquer 7-8 secondes

3. **Pendant le blocage:**
   - Nouvelles données arrivent toutes les 500ms
   - Calcul: 8 secondes / 0.5 seconde = **16 nouvelles données**
   - Queue de 25 slots se remplit rapidement

4. **Quand la queue est pleine:**
   - `xQueueSendToBack()` retourne `pdFALSE` (`src/app_tasks.cpp:188`)
   - Message "Queue pleine - donnée de capteur perdue!" (ligne 189)

**Code concerné:** `src/app_tasks.cpp:185-189`

```cpp
BaseType_t result = xQueueSendToBack(g_sensorQueue,
                                     &readings,
                                     pdMS_TO_TICKS(200));
if (result != pdTRUE) {
  SENSOR_LOG_PRINTLN(F("[Sensor] ⚠️ Queue pleine - donnée de capteur perdue!"));
}
```

### Pourquoi seulement 19 erreurs en 4 minutes 48 secondes ?

**Calcul:**
- 19 erreurs sur 288 secondes = ~1 erreur toutes les 15 secondes
- La queue se remplit seulement quand le DHT22 bloque
- Les blocages DHT22 ne sont pas continus (timeouts de 3-5 secondes)
- Entre les blocages, la queue se vide (tâche `automationTask` consomme les données)

**Explication:**
- **19 erreurs** = 19 moments où la queue était pleine au moment de l'envoi
- La queue se remplit pendant les blocages DHT22 (5-8 secondes)
- Entre les blocages, la queue se vide progressivement
- Le nombre d'erreurs dépend de la fréquence et durée des blocages

---

## Relations Entre les Problèmes

### Cascade de défaillances

```
DHT22 défaillant
    ↓
Blocages 5-8 secondes
    ↓
Queue se remplit (19 erreurs)
    ↓
Watchdog non reset → Timeout 5 minutes
    ↓
Redémarrage
    ↓
Initialisation consomme mémoire
    ↓
Heap minimum = 25 KB (< 52 KB requis)
    ↓
Tentative TLS échoue → Panic
    ↓
Redémarrage (boucle)
```

### Problème principal

**Le DHT22 défaillant est le déclencheur** de tous les autres problèmes :

1. **Blocages → Queue pleine**
   - DHT22 bloque 5-8 secondes
   - Queue se remplit (16+ nouvelles données pendant le blocage)
   - 19 erreurs "Queue pleine" détectées

2. **Blocages → Watchdog timeout → Redémarrages**
   - Pendant les blocages, watchdog non reset
   - Si 40+ blocages: 40 × 7s = 280 secondes → proche timeout 300s
   - Watchdog timeout → redémarrage

3. **Redémarrages → Consommation mémoire → Heap critique**
   - Chaque redémarrage consomme de la mémoire (initialisation)
   - Fragmentation mémoire s'aggrave
   - Heap minimum se stabilise à ~25 KB

4. **Heap critique → Panic → Redémarrages en boucle**
   - Tentative opération TLS (requiert 52 KB)
   - Échec allocation → Panic
   - Redémarrage → retour à l'étape 1

---

## Note sur la Désactivation DHT22

### Mécanisme existant

**Fichier:** `src/sensors.cpp:872-877`

```cpp
// 5. Désactiver le capteur après MAX_CONSECUTIVE_FAILURES échecs
if (_consecutiveFailures >= MAX_CONSECUTIVE_FAILURES && !_disableLogged) {
  _sensorDisabled = true;
  _disableLogged = true;
  SENSOR_LOG_PRINTF("[AirSensor] 🔴 DHT22 désactivé après %d échecs consécutifs...\n",
                    MAX_CONSECUTIVE_FAILURES, ...);
}
```

**Configuration:** `include/sensors.h:91`

```cpp
static constexpr uint8_t MAX_CONSECUTIVE_FAILURES = 10;
```

### Pourquoi la désactivation ne fonctionne pas ?

**Problème identifié:**

1. **Compteur partagé entre deux méthodes:**
   - `robustTemperatureC()` utilise `_consecutiveFailures`
   - `robustHumidity()` utilise le **même** compteur `_consecutiveFailures`
   - Les deux méthodes sont appelées dans le même cycle (`src/system_sensors.cpp:67, 82`)

2. **Réinitialisation prématurée:**
   - Si `robustTemperatureC()` échoue mais `robustHumidity()` réussit → compteur réinitialisé à 0
   - Si `robustHumidity()` échoue mais `robustTemperatureC()` réussit → compteur réinitialisé à 0
   - Le compteur ne peut jamais atteindre 10 consécutifs si une des deux méthodes réussit occasionnellement

3. **Réinitialisations dans le code:**
   - `src/sensors.cpp:791` - Réinitialisé si `filteredTemperatureC()` réussit
   - `src/sensors.cpp:822` - Réinitialisé après récupération réussie (température)
   - `src/sensors.cpp:852` - Réinitialisé après récupération réussie (température)
   - `src/sensors.cpp:953` - Réinitialisé si `filteredHumidity()` réussit
   - `src/sensors.cpp:979` - Réinitialisé après récupération réussie (humidité)
   - `src/sensors.cpp:1008` - Réinitialisé après récupération réussie (humidité)

**Résultat:**
- 154 échecs détectés mais **0 désactivation**
- Le compteur est réinitialisé trop souvent pour atteindre 10 consécutifs
- Le mécanisme de désactivation ne peut jamais se déclencher

---

## Recommandations

### Priorité 1: Désactiver le DHT22

1. **Vérifier le mécanisme de désactivation:**
   - Séparer les compteurs pour température et humidité
   - OU: Désactiver si l'une des deux méthodes échoue 10 fois consécutivement
   - OU: Désactiver manuellement si le capteur est déconnecté

2. **Désactivation immédiate:**
   - Désactiver le DHT22 manuellement dans la configuration
   - OU: Réduire `MAX_CONSECUTIVE_FAILURES` à 5 pour déclencher plus rapidement

### Priorité 2: Réduire la fragmentation mémoire

1. **Remplacer String Arduino par char[]:**
   - ~442 occurrences de String Arduino dans le code
   - Utiliser des buffers statiques ou char[] à la place

2. **Réduire les allocations temporaires:**
   - Réutiliser les buffers HTTP/HTTPS
   - Réduire la taille des historiques de capteurs

3. **Optimiser les queues:**
   - Réduire la taille de la queue capteurs si possible
   - OU: Augmenter la partition heap disponible

### Priorité 3: Améliorer la gestion du watchdog

1. **Reset watchdog pendant les blocages:**
   - Ajouter `esp_task_wdt_reset()` dans les boucles de récupération DHT22
   - OU: Utiliser une tâche séparée pour le watchdog reset

2. **Réduire le timeout watchdog:**
   - Réduire à 2-3 minutes pour détecter les blocages plus rapidement
   - OU: Améliorer la gestion des blocages pour éviter les timeouts

### Priorité 4: Améliorer la gestion de la queue

1. **Augmenter la taille de la queue:**
   - Passer de 25 à 50 slots si la mémoire le permet
   - OU: Implémenter un mécanisme de priorité (perdre les données anciennes)

2. **Désactiver le DHT22:**
   - Si le DHT22 est désactivé, les blocages disparaissent
   - La queue ne se remplira plus

---

## Conclusion

L'analyse confirme que **le capteur DHT22 défaillant est la cause racine** de tous les problèmes critiques :

1. Les blocages DHT22 (5-8 secondes) provoquent la saturation de la queue
2. Les blocages empêchent le reset du watchdog, causant des timeouts et redémarrages
3. Les redémarrages répétés aggravent la fragmentation mémoire
4. La fragmentation mémoire empêche les opérations TLS, causant des panics
5. Les panics provoquent des redémarrages en boucle

**Action immédiate requise:** Désactiver ou corriger le mécanisme de désactivation du DHT22 pour briser la cascade de défaillances.

---

**Fichiers de référence:**
- Log complet: `monitor_15min_v11.156_2026-01-21_22-29-41.log`
- Analyse automatique: `monitor_15min_v11.156_2026-01-21_22-29-41_analysis.txt`
- Rapport monitoring: `RAPPORT_MONITORING_2026-01-21.md`
