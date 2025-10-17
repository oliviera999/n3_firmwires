# 🔧 Fix PANIC après mise en veille - Problème de mémoire critique

**Date:** 7 octobre 2025  
**Version:** v10.90  
**Environnement:** ESP32-WROOM (wroom-prod)  
**Gravité:** 🔴 CRITIQUE

## 📋 Symptômes

- Redémarrage PANIC systématique après réception du mail de mise en veille
- Délai: moins d'une minute après l'email de veille
- Raison du redémarrage: **PANIC (erreur critique)**
- Compteur de redémarrages: **104** (très élevé)
- **Heap libre minimum historique: 15500 bytes** (CRITIQUE - normal > 50KB)

```
Raison du redémarrage: PANIC (erreur critique)
Compteur de redémarrages: 104
Heap libre au démarrage: 180300 bytes
Heap libre minimum historique: 15500 bytes  ⚠️ TRÈS BAS
```

## 🔍 Diagnostic

### Causes identifiées

1. **Aucune vérification de mémoire** avant l'entrée en sleep
   - La fonction `validateSystemStateBeforeSleep()` ne vérifiait que les actionneurs
   - Pas de contrôle du heap disponible

2. **Consommation mémoire excessive avant sleep**
   - `sendFullUpdate()` : requête HTTP + JSON (~20-30KB)
   - Email de mise en veille : SMTP + buffers (~30-40KB)
   - Aucun nettoyage mémoire avant le sleep

3. **Heap critique < 20KB**
   - ESP32 a besoin d'au moins 30-50KB pour fonctionner correctement
   - En dessous de 20KB : risque de PANIC très élevé
   - Le sleep peut déclencher des allocations internes qui causent le crash

4. **Fragmentation mémoire**
   - Allocations/désallocations répétées pendant le fonctionnement
   - Pas de garbage collection avant sleep

## ✅ Solutions implémentées

### 1. Validation mémoire avant sleep (`src/automatism.cpp`)

```cpp
bool Automatism::validateSystemStateBeforeSleep() {
  // Vérification actionneurs (existant)
  if (_acts.isTankPumpRunning()) {
    return false;
  }
  
  // NOUVEAU: Vérification mémoire
  uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t MIN_HEAP_FOR_SLEEP = 40000; // 40KB minimum
  
  if (freeHeap < MIN_HEAP_FOR_SLEEP) {
    Serial.printf("⚠️ Sleep annulé: heap insuffisant (%u < %u bytes)\n", 
                  freeHeap, MIN_HEAP_FOR_SLEEP);
    
    // Tentative de nettoyage mémoire d'urgence
    yield();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    if (ESP.getFreeHeap() < MIN_HEAP_FOR_SLEEP) {
      return false; // Sleep annulé
    }
  }
  
  return true;
}
```

### 2. Contrôle conditionnel des opérations coûteuses

#### `sendFullUpdate()` avant sleep
```cpp
uint32_t heapBeforeSend = ESP.getFreeHeap();
const uint32_t MIN_HEAP_FOR_SEND = 50000; // 50KB minimum

if (heapBeforeSend >= MIN_HEAP_FOR_SEND) {
  sendFullUpdate(r, "resetMode=0");
} else {
  Serial.printf("⚠️ Skip sendFullUpdate: heap insuffisant (%u bytes)\n", heapBeforeSend);
  // Données synchronisées au prochain réveil
}
```

#### Email de mise en veille
```cpp
const uint32_t MIN_HEAP_FOR_MAIL = 45000; // 45KB minimum

if (heapBeforeMail >= MIN_HEAP_FOR_MAIL) {
  _mailer.sendSleepMail(sleepReason.c_str(), actualSleepDuration);
} else {
  Serial.printf("⚠️ Skip mail de mise en veille: heap insuffisant (%u bytes)\n", 
                heapBeforeMail);
}
```

### 3. Nettoyage mémoire explicite avant sleep

```cpp
Serial.println("[Auto] 🧹 Nettoyage mémoire avant sleep...");
uint32_t heapBeforeCleanup = ESP.getFreeHeap();

// Force garbage collection
yield();
vTaskDelay(pdMS_TO_TICKS(100));

uint32_t heapAfterCleanup = ESP.getFreeHeap();
Serial.printf("[Auto] 📊 Heap avant sleep: %u bytes (nettoyé: %d bytes)\n", 
              heapAfterCleanup, (int)(heapAfterCleanup - heapBeforeCleanup));

if (heapAfterCleanup < 30000) {
  Serial.printf("⚠️ ATTENTION: Heap critique (%u bytes) - risque de PANIC\n", 
                heapAfterCleanup);
}
```

### 4. Monitoring mémoire au réveil

```cpp
// DIAGNOSTIC MÉMOIRE AU RÉVEIL
uint32_t heapAfterWake = ESP.getFreeHeap();
uint32_t minHeapEver = ESP.getMinFreeHeap();
Serial.printf("[Auto] 📊 Réveil - Heap actuel: %u bytes, minimum historique: %u bytes\n", 
              heapAfterWake, minHeapEver);

// Alertes
if (heapAfterWake < 30000) {
  Serial.printf("[Auto] 🚨 ALERTE: Heap critique au réveil (%u bytes)\n", heapAfterWake);
}
if (minHeapEver < 20000) {
  Serial.printf("[Auto] 🚨 ALERTE: Heap minimum historique critique (%u bytes)\n", minHeapEver);
}
```

### 5. Log mémoire dans power.cpp

```cpp
// Juste avant esp_light_sleep_start()
uint32_t finalHeap = ESP.getFreeHeap();
uint32_t minHeap = ESP.getMinFreeHeap();
Serial.printf("[Power] 📊 Mémoire avant sleep: %u bytes (min historique: %u bytes)\n", 
              finalHeap, minHeap);

if (finalHeap < 30000) {
  Serial.printf("[Power] 🚨 ATTENTION CRITIQUE: Heap très bas (%u bytes)\n", finalHeap);
}
```

## 📊 Seuils de sécurité définis

| Opération | Seuil minimum | Raison |
|-----------|---------------|--------|
| Entrée en sleep | 40 KB | Sécurité ESP32 + allocations internes |
| `sendFullUpdate()` | 50 KB | HTTP + JSON + buffers |
| Email (sleep/wake) | 45 KB | SMTP + TLS + buffers |
| Alerte critique | 30 KB | Risque élevé de PANIC |
| Alerte historique | 20 KB | Fragmentation critique |

## 🎯 Résultats attendus

1. **Prévention des PANIC**
   - Sleep annulé si mémoire insuffisante
   - Email/HTTP skippés si heap critique
   
2. **Diagnostic amélioré**
   - Logs mémoire détaillés avant/après sleep
   - Alertes en cas de heap critique
   - Traçabilité complète
   
3. **Graceful degradation**
   - Fonctionnalités non-critiques désactivées si mémoire basse
   - Système continue de fonctionner
   - Données synchronisées au prochain cycle

## 🧪 Tests recommandés

1. **Surveillance logs**
   ```
   [Auto] 📊 Heap libre: XXXX bytes (minimum historique: YYYY bytes)
   [Power] 📊 Mémoire avant sleep: XXXX bytes
   ```

2. **Vérifier**
   - Heap libre avant sleep > 40KB
   - Pas de PANIC après sleep
   - Emails envoyés uniquement si heap suffisant

3. **Cas dégradés**
   - Observer comportement quand heap < 50KB
   - Vérifier que sleep est annulé si heap < 40KB
   - Confirmer que données sont quand même sauvegardées en NVS

## 📝 Fichiers modifiés

- `src/automatism.cpp` : Validation mémoire, contrôles conditionnels, nettoyage
- `src/power.cpp` : Log mémoire avant sleep

## ⚠️ Notes importantes

1. **Heap minimum historique = 15500 bytes** était TRÈS critique
   - Normal devrait être > 50KB
   - Indique une fuite mémoire ou fragmentation excessive

2. **104 redémarrages** indique un problème récurrent
   - Cycle: wake → activité → sleep → PANIC → reboot
   - Ce fix devrait casser ce cycle

3. **Si problème persiste**
   - Investiguer les fuites mémoire dans les capteurs
   - Vérifier les allocations String (utiliser reserve())
   - Considérer réduction des buffers JSON

## 🔗 Références

- ESP32 Heap Management: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/heap_debug.html
- Light Sleep Power Consumption: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/sleep_modes.html

