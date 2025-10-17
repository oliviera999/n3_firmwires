# 🚀 Optimisations Mémoire Optionnelles

**Statut:** À appliquer UNIQUEMENT si le problème de mémoire persiste après le fix principal

---

## 📊 Analyse de la consommation actuelle

### RAM au démarrage
```
RAM:   [==        ]  19.4% (used 63436 bytes from 327680 bytes)
Flash: [========  ]  82.4% (used 1673151 bytes from 2031616 bytes)
```

**RAM libre au démarrage:** ~264 KB  
**Heap minimum observé:** 15.5 KB ⚠️ (problème critique corrigé)  
**Différence:** ~248 KB consommés pendant le fonctionnement

---

## 🔍 Sources de consommation mémoire

### 1. Capteurs (sensors.cpp)

#### Filtrage avancé des capteurs
- **UltrasonicManager**: Historique + filtrage médiane
- **WaterTempSensor**: Pipeline + historique + NVS
- **AirSensor**: EMA + historique DHT

#### Buffers de lecture
```cpp
uint16_t readings[READINGS_COUNT];     // 5 × 2 bytes = 10 bytes
float readings[READINGS_COUNT];        // 5 × 4 bytes = 20 bytes
```

### 2. JSON Documents

```cpp
StaticJsonDocument<4096> doc;  // 4 KB par instance
```

**Utilisations:**
- Configuration distante
- Réponses API
- État système

### 3. Strings dynamiques

```cpp
String sleepReason;
sleepReason.reserve(256);  // 256 bytes pré-alloués
```

### 4. HTTP/SMTP

- Buffers temporaires pour requêtes
- TLS/SSL handshake
- Headers HTTP

---

## 🛠️ Optimisations à considérer

### Option 1: Réduire les échantillons de capteurs

**Gain estimé:** ~100-200 bytes par lecture

#### Ultrason (sensors.h)
```cpp
// Actuel
static constexpr uint8_t READINGS_COUNT = 5;
static constexpr uint8_t HISTORY_SIZE = 5;

// Optimisé
static constexpr uint8_t READINGS_COUNT = 3;  // -4 bytes
static constexpr uint8_t HISTORY_SIZE = 3;    // -4 bytes
```

#### Température eau (sensors.h)
```cpp
// Actuel
static constexpr uint8_t READINGS_COUNT = 5;
static constexpr uint8_t HISTORY_SIZE = 5;

// Optimisé
static constexpr uint8_t READINGS_COUNT = 3;  // -8 bytes (float)
static constexpr uint8_t HISTORY_SIZE = 3;    // -8 bytes (float)
```

**Impact qualité:** Minime - 3 échantillons suffisent pour un filtrage médiane efficace

---

### Option 2: Réduire les buffers JSON

**Gain estimé:** ~2 KB par instance

#### Dans automatism.cpp
```cpp
// Actuel (ligne ~3128)
StaticJsonDocument<4096> tmpDoc;

// Optimisé
StaticJsonDocument<2048> tmpDoc;  // -2048 bytes
```

**Vérification:** Analyser la taille réelle des JSON reçus
```cpp
Serial.printf("JSON size: %zu bytes\n", tmpDoc.memoryUsage());
```

Si < 2048 bytes, réduction safe.

---

### Option 3: Optimiser les Strings

**Gain estimé:** ~100-500 bytes selon fragmentation

#### Email de mise en veille (automatism.cpp ligne ~3021)
```cpp
// Actuel
String sleepReason;
sleepReason.reserve(256);
sleepReason += "Délai écoulé...";
// ... multiples concaténations

// Optimisé - Utiliser snprintf
char sleepReasonBuf[256];
snprintf(sleepReasonBuf, sizeof(sleepReasonBuf),
         "Délai écoulé (éveillé %lus, cible %us)%s - aucune activité...",
         awakeSec, adaptiveDelay, 
         tideAscendingTrigger ? " + marée montante" : "");

_mailer.sendSleepMail(sleepReasonBuf, actualSleepDuration);
```

**Avantages:**
- Pas d'allocation dynamique
- Pas de fragmentation
- Stack au lieu du heap

---

### Option 4: Limiter les conversions pipeline DS18B20

**Gain estimé:** ~50-100 bytes pendant lecture

#### WaterTempSensor::filteredTemperatureC() (sensors.cpp)
```cpp
// Actuel: READINGS_COUNT = 5 lectures avec pipeline
// Optimisé: Réduire à 3 lectures

static constexpr uint8_t READINGS_COUNT = 3;  // Au lieu de 5
```

---

### Option 5: Réduire les délais de stabilisation

**Gain temps:** Pas de gain mémoire mais réduit le temps où la mémoire est occupée

#### Dans project_config.h (ExtendedSensorConfig)
```cpp
// Actuel
static constexpr uint16_t DS18B20_STABILIZATION_DELAY_MS = 100;

// Optimisé (si capteur stable)
static constexpr uint16_t DS18B20_STABILIZATION_DELAY_MS = 50;
```

**Attention:** Tester la stabilité des lectures après modification

---

### Option 6: Désactiver fonctionnalités non-critiques en mode low-memory

**Gain:** Variable selon contexte

#### Ajouter un mode "low memory" global
```cpp
// Dans automatism.h
bool isLowMemoryMode() const {
  return ESP.getFreeHeap() < 60000;
}

// Utiliser dans le code
if (!isLowMemoryMode()) {
  // Fonctionnalités gourmandes
  _mailer.sendSleepMail(...);
  sendFullUpdate(...);
}
```

---

### Option 7: Restart préventif

**Dernier recours** si fuites mémoire persistantes

```cpp
// Dans automatism.cpp, dans loop() ou handleAutoSleep()
uint32_t minHeap = ESP.getMinFreeHeap();
if (minHeap < 25000) {
  Serial.printf("[Auto] 🔄 Restart préventif - heap critique (%u bytes)\n", minHeap);
  
  // Sauvegarde état
  _config.saveBouffeFlags();
  saveFeedingState();
  
  // Email d'alerte (optionnel)
  #if FEATURE_MAIL
  if (WiFi.status() == WL_CONNECTED) {
    _mailer.sendDebugMail("Restart préventif heap critique", 
                          String("Heap min: ") + String(minHeap));
  }
  #endif
  
  vTaskDelay(pdMS_TO_TICKS(2000));
  ESP.restart();
}
```

---

## 📋 Ordre d'application recommandé

### Si heap min reste < 30KB après fix principal:

1. **[FACILE]** Option 6: Mode low-memory (skip email/HTTP si heap bas)
   - Déjà implémenté dans le fix principal ✅
   
2. **[FACILE]** Option 1: Réduire échantillons capteurs (5→3)
   - Impact minimal sur qualité
   - Gain: ~50-100 bytes
   
3. **[MOYEN]** Option 3: Remplacer Strings par buffers statiques
   - Impact qualité: aucun
   - Gain: ~200-500 bytes
   
4. **[MOYEN]** Option 2: Réduire buffers JSON (4KB→2KB)
   - Vérifier tailles JSON d'abord
   - Gain: ~2KB
   
5. **[DERNIER RECOURS]** Option 7: Restart préventif
   - Si fuites mémoire non identifiables

### Si heap min 30-40KB (limite):

1. Option 1 (échantillons)
2. Option 3 (Strings)

### Si heap min > 40KB:

✅ Aucune optimisation supplémentaire nécessaire

---

## 🧪 Validation après optimisation

### Vérifications obligatoires:

1. **Qualité des mesures**
   ```
   - Température eau: ±0.5°C max
   - Température air: ±1°C max
   - Niveau eau: ±2cm max
   ```

2. **Stabilité système**
   ```
   - Uptime > 24h sans crash
   - Pas de PANIC
   - Sleep fonctionnel
   ```

3. **Gains mémoire**
   ```
   - Heap minimum > 40KB
   - Heap avant sleep > 50KB
   - Pas de décroissance progressive
   ```

---

## 📊 Tableau récapitulatif

| Option | Gain mémoire | Complexité | Impact qualité | Priorité |
|--------|--------------|------------|----------------|----------|
| 1. Échantillons ↓ | 50-100B | ★☆☆ | Faible | Haute |
| 2. JSON ↓ | 2KB | ★★☆ | Aucun | Moyenne |
| 3. Strings → buffers | 200-500B | ★★☆ | Aucun | Haute |
| 4. Pipeline DS18B20 ↓ | 50-100B | ★☆☆ | Faible | Basse |
| 5. Délais ↓ | 0B (temps) | ★☆☆ | Potentiel | Basse |
| 6. Mode low-memory | Variable | ★★★ | Graceful | ✅ Déjà fait |
| 7. Restart préventif | N/A | ★☆☆ | Aucun | Dernier recours |

---

## ⚠️ Notes importantes

### À NE PAS faire

1. ❌ Désactiver le watchdog (risque de blocages)
2. ❌ Augmenter HISTORY_SIZE (plus de mémoire)
3. ❌ Ajouter plus de buffers temporaires
4. ❌ Utiliser String sans reserve()
5. ❌ Ignorer les alertes heap critique

### Bonnes pratiques

1. ✅ Toujours tester après chaque modification
2. ✅ Surveiller heap min pendant 24h
3. ✅ Documenter les changements
4. ✅ Garder les logs de mémoire actifs
5. ✅ Appliquer une optimisation à la fois

---

## 🎯 Objectif final

**Heap minimum stable > 40KB** sur plusieurs jours

- Permet le sleep sans risque
- Marge de sécurité confortable
- Pas de mode dégradé fréquent
- Système résilient

---

## 📞 Quand appliquer ces optimisations ?

### ✅ Appliquer si:
- Heap min < 30KB malgré le fix principal
- Sleep annulé régulièrement (heap < 40KB)
- Mode dégradé fréquent

### ⏸️ Attendre si:
- Heap min > 40KB stable
- Sleep fonctionne correctement
- Pas de PANIC observé

### 🔍 Investiguer d'abord:
- Fuites mémoire évidentes
- Buffers qui grossissent
- Allocations non libérées

---

**💡 Conseil:** Commencez par observer le système avec le fix principal pendant 24-48h avant d'appliquer ces optimisations. Elles ne sont nécessaires que si le problème persiste.

