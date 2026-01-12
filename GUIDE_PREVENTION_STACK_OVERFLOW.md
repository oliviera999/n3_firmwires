# 🛡️ Guide de Prévention des Stack Overflows - Projet ESP32 FFP5CS

## 📋 Vue d'ensemble

Ce guide présente les meilleures pratiques pour prévenir les problèmes de stack overflow dans ce projet ESP32, basé sur l'analyse du code existant et les incidents documentés.

## 🔍 État Actuel des Configurations

### Tailles de Stack Actuelles (config.h)

```cpp
namespace TaskConfig {
    constexpr uint32_t SENSOR_TASK_STACK_SIZE = 4096;      // 4 KB
    constexpr uint32_t WEB_TASK_STACK_SIZE = 6144;          // 6 KB
    constexpr uint32_t AUTOMATION_TASK_STACK_SIZE = 8192;   // 8 KB ⚠️ Problème connu
    constexpr uint32_t DISPLAY_TASK_STACK_SIZE = 4096;      // 4 KB
    constexpr uint32_t OTA_TASK_STACK_SIZE = 8192;          // 8 KB
}
```

### ⚠️ Problème Identifié

La tâche `automationTask` (8192 bytes) a déjà causé des stack overflows pendant les handshakes SSL/TLS (voir `DIAGNOSTIC_STACK_OVERFLOW.md`).

## 🎯 Stratégies de Prévention

### 1. Monitoring Actif de la Stack

#### ✅ Déjà Implémenté (Mode TEST uniquement)

Le code actuel dans `src/app_tasks.cpp` inclut un monitoring dans `automationTask` mais uniquement en mode TEST :

```cpp
#if defined(PROFILE_TEST)
  unsigned long lastStackCheck = 0;
  const unsigned long stackCheckInterval = 30000;
  
  if (now - lastStackCheck > stackCheckInterval) {
    UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(nullptr);
    // ... logs ...
  }
#endif
```

#### 🔧 Recommandation : Monitoring Permanent avec Logs Conditionnels

**Option A : Monitoring permanent avec logs désactivables**

Dans `src/app_tasks.cpp`, remplacer le `#if defined(PROFILE_TEST)` par un système basé sur un flag de configuration :

```cpp
// Dans include/config.h
namespace StackMonitorConfig {
    constexpr bool ENABLE_STACK_MONITORING = true;  // Toujours actif
    constexpr bool VERBOSE_STACK_LOGS = false;       // Logs détaillés uniquement en TEST
    constexpr unsigned long STACK_CHECK_INTERVAL_MS = 60000; // 1 minute
}

// Dans automationTask
#if StackMonitorConfig::ENABLE_STACK_MONITORING
  static unsigned long lastStackCheck = 0;
  unsigned long now = millis();
  if (now - lastStackCheck > StackMonitorConfig::STACK_CHECK_INTERVAL_MS) {
    UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(nullptr);
    uint32_t stackUsed = TaskConfig::AUTOMATION_TASK_STACK_SIZE - stackHighWaterMark;
    float stackPercent = (1.0 - (float)stackHighWaterMark / TaskConfig::AUTOMATION_TASK_STACK_SIZE) * 100.0;
    
    #if StackMonitorConfig::VERBOSE_STACK_LOGS || defined(PROFILE_TEST)
    Serial.printf("[autoTask] Stack HWM: %u bytes libres (sur %u bytes)\n", 
                  stackHighWaterMark, TaskConfig::AUTOMATION_TASK_STACK_SIZE);
    Serial.printf("[autoTask] Stack utilisée: %u bytes (%.1f%%)\n", stackUsed, stackPercent);
    #endif
    
    // Alertes critiques toujours loggées
    if (stackPercent > 85.0) {
      Serial.printf("[autoTask] ⚠️ CRITIQUE: Stack utilisation > 85%% (%u bytes libres)\n", 
                    stackHighWaterMark);
      EventLog::add("CRITICAL: Stack usage > 85% in automationTask");
    } else if (stackPercent > 70.0) {
      #if StackMonitorConfig::VERBOSE_STACK_LOGS || defined(PROFILE_TEST)
      Serial.printf("[autoTask] ⚠️ ATTENTION: Stack utilisation > 70%% (%u bytes libres)\n", 
                    stackHighWaterMark);
      #endif
    }
    
    lastStackCheck = now;
  }
#endif
```

**Option B : Monitoring au démarrage et dans diagnostics**

Ajouter un monitoring périodique dans `diagnostics.cpp` pour toutes les tâches :

```cpp
// Dans diagnostics.cpp::update()
static unsigned long lastStackCheck = 0;
const unsigned long stackCheckInterval = 300000; // 5 minutes

if (now - lastStackCheck > stackCheckInterval) {
  if (g_autoTaskHandle) {
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(g_autoTaskHandle);
    uint32_t used = TaskConfig::AUTOMATION_TASK_STACK_SIZE - hwm;
    float percent = (1.0 - (float)hwm / TaskConfig::AUTOMATION_TASK_STACK_SIZE) * 100.0;
    
    if (percent > 85.0) {
      Serial.printf("[Diagnostics] ⚠️ CRITIQUE autoTask stack: %.1f%% utilisée (%u bytes libres)\n", 
                    percent, hwm);
    }
  }
  lastStackCheck = now;
}
```

### 2. Activation du Hook de Stack Overflow FreeRTOS

#### Configuration dans platformio.ini

Ajouter dans la section `[env]` ou dans chaque environnement :

```ini
build_flags = 
    ${env.build_flags}
    -DconfigCHECK_FOR_STACK_OVERFLOW=2  # Mode 2 = Détection avancée avec canary
```

**Modes disponibles :**
- `0` : Désactivé (défaut)
- `1` : Détection simple (moins précis)
- `2` : Détection avancée avec canary (recommandé)

#### Implémentation du Hook

Créer ou ajouter dans `src/system_boot.cpp` ou `src/app.cpp` :

```cpp
extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  // Désactiver les interruptions pour éviter les problèmes
  portDISABLE_INTERRUPTS();
  
  // Logs d'urgence (utilisation directe de Serial si possible)
  Serial.printf("\n\n⚠️⚠️⚠️ STACK OVERFLOW DÉTECTÉ ⚠️⚠️⚠️\n");
  Serial.printf("Tâche: %s\n", pcTaskName ? pcTaskName : "UNKNOWN");
  Serial.printf("Handle: %p\n", xTask);
  
  // Tenter de sauvegarder dans EventLog (si possible)
  #ifdef FEATURE_EVENT_LOG
  EventLog::add(String("CRITICAL: Stack overflow in ") + (pcTaskName ? pcTaskName : "UNKNOWN"));
  #endif
  
  // Attendre un peu pour permettre l'écriture série
  delay(100);
  
  // Le système va reboot automatiquement après
  // Ne pas appeler esp_restart() ici, laisser FreeRTOS gérer
}
```

### 3. Réduction de l'Allocation Stack

#### ❌ À ÉVITER : Allocations locales volumineuses

```cpp
// ❌ MAUVAIS - Alloue sur la stack
void automationTask(void* pv) {
  char largeBuffer[2048];              // ❌ 2 KB sur la stack
  JsonDocument doc(4096);              // ❌ 4 KB sur la stack
  String tempString;                   // ❌ Fragmentation, allocation stack
}

// ❌ MAUVAIS - Buffer local récursif
void processData() {
  char buffer[1024];
  // Appels récursifs = stack cumulative
  processData(); // ❌
}
```

#### ✅ BONNES PRATIQUES : Allocation statique ou heap

```cpp
// ✅ BON - Allocation statique (une seule fois)
void automationTask(void* pv) {
  static char largeBuffer[2048];       // ✅ Alloué une seule fois
  static JsonDocument doc(4096);       // ✅ Alloué une seule fois
  // Réutiliser le même buffer à chaque itération
}

// ✅ BON - Allocation sur heap (si vraiment nécessaire)
void processData() {
  char* buffer = (char*)malloc(2048);  // ✅ Heap, mais attention fragmentation
  if (buffer) {
    // Utiliser buffer
    free(buffer);  // ✅ Libérer immédiatement
  }
}

// ✅ MEILLEUR - Buffer global ou membre de classe
namespace {
  char g_sharedBuffer[2048];  // Allocation globale
}

void automationTask(void* pv) {
  // Utiliser g_sharedBuffer (protéger avec mutex si multithread)
}
```

#### 🔍 Zones à Vérifier dans le Code

1. **automatism_network.cpp** :
   - `char payloadBuffer[1024]` (ligne 253) ✅ OK si local
   - `ArduinoJson::DynamicJsonDocument tmp(BufferConfig::JSON_DOCUMENT_SIZE)` (ligne 508) ⚠️ Vérifier taille
   - `String jsonStr` (ligne 94) ⚠️ Préférer char[]

2. **automatism_refill_controller.cpp** :
   - `char msg[512]` et `char msg[1024]` ⚠️ Vérifier si récursif

3. **Fonctions SSL/TLS** :
   - Les handshakes SSL consomment 2-4 KB de stack
   - Considérer augmenter AUTOMATION_TASK_STACK_SIZE si nécessaire

### 4. Optimisation des Connexions SSL/TLS

#### Problème Connu

Les handshakes SSL/TLS consomment beaucoup de stack (2-4 KB). La tâche `automationTask` effectue des connexions HTTPS.

#### Solutions

**Option A : Augmenter la stack (solution rapide)**

```cpp
// Dans include/config.h
constexpr uint32_t AUTOMATION_TASK_STACK_SIZE = 12288;  // 12 KB (+50%)
```

⚠️ **Attention** : Réduit la mémoire heap disponible.

**Option B : Réduire la complexité SSL**

Si possible, utiliser des connexions HTTP simples pour certaines opérations non-critiques.

**Option C : Déléguer les opérations SSL à une tâche dédiée**

Créer une tâche dédiée avec une stack plus grande pour les opérations SSL :

```cpp
constexpr uint32_t SSL_TASK_STACK_SIZE = 16384;  // 16 KB dédié SSL
```

### 5. Limitation de la Profondeur d'Appels

#### Vérification de la Profondeur

Éviter les appels de fonctions trop profonds qui s'empilent sur la stack :

```cpp
// ❌ MAUVAIS - Profondeur excessive
void a() { b(); }
void b() { c(); }
void c() { d(); }
// ... 10+ niveaux

// ✅ BON - Restructuration avec état
enum class State { A, B, C, D };
State currentState = State::A;

void process() {
  switch (currentState) {
    case State::A: currentState = State::B; break;
    case State::B: currentState = State::C; break;
    // Pas d'empilement
  }
}
```

### 6. Validation des Tailles de Stack

#### Recommandations par Type de Tâche

| Tâche | Stack Actuel | Recommandation | Justification |
|-------|--------------|----------------|---------------|
| `sensorTask` | 4096 | 4096-6144 | Lecture capteurs simple, pas de SSL |
| `webTask` | 6144 | 6144-8192 | Serveur web, WebSocket (modéré) |
| `automationTask` | 8192 | **12288-16384** | SSL/TLS, JSON parsing, logique complexe |
| `displayTask` | 4096 | 4096 | Affichage OLED simple |
| `otaTask` | 8192 | 8192-12288 | Téléchargement firmware |

#### Calcul de Stack Requise

Formule approximative :
```
Stack requise = Stack de base (2 KB) 
              + Variables locales max
              + Profondeur d'appels × 256 bytes
              + SSL/TLS overhead (2-4 KB si applicable)
              + Marge de sécurité (20-30%)
```

### 7. Tests et Validation

#### Test de Stress Stack

Créer un script de test pour forcer l'utilisation maximale de stack :

```cpp
// Dans automationTask (uniquement en mode TEST)
#ifdef PROFILE_TEST
  // Test de charge stack (désactiver en production)
  static bool stackTestRun = false;
  if (!stackTestRun) {
    stackTestRun = true;
    // Simuler une charge maximale
    char testBuffer[4096];  // Utiliser beaucoup de stack
    memset(testBuffer, 0, sizeof(testBuffer));
    // Vérifier le HWM après
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(nullptr);
    Serial.printf("[StackTest] HWM après charge: %u bytes\n", hwm);
  }
#endif
```

#### Monitoring Post-Déploiement

Après chaque déploiement, vérifier les logs pour :
- Stack HWM < 1024 bytes → **CRITIQUE**
- Stack HWM < 2048 bytes → **ATTENTION**
- Stack HWM > 2048 bytes → ✅ OK

### 8. Scripts de Monitoring

#### Script PowerShell de Monitoring Stack

Créer `monitor_stack_usage.ps1` :

```powershell
# Monitor Stack Usage
$logFile = "monitor_stack_usage_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"
$patterns = @(
    "Stack HWM",
    "Stack utilisée",
    "Stack.*bytes",
    "STACK OVERFLOW",
    "autoTask.*Stack"
)

Write-Host "Monitoring stack usage... (Ctrl+C pour arrêter)"
Write-Host "Log file: $logFile"

pio device monitor --port COM4 --baud 115200 | Tee-Object -FilePath $logFile | 
    Select-String -Pattern ($patterns -join '|') -Context 1,1
```

## 📊 Interprétation des Résultats

### Seuils d'Alerte

| Utilisation Stack | Bytes Libres | Statut | Action |
|-------------------|--------------|--------|--------|
| < 70% | > 2457 bytes | ✅ **OK** | Aucune action |
| 70-85% | 1228-2457 bytes | ⚠️ **ATTENTION** | Surveiller, optimiser si possible |
| 85-95% | 409-1228 bytes | ⚠️⚠️ **RISQUE** | Augmenter stack ou optimiser code |
| > 95% | < 409 bytes | ❌ **CRITIQUE** | Augmenter stack immédiatement |

### Exemple de Logs

```
[autoTask] Stack HWM: 1843 bytes libres (sur 8192 bytes configurés)
[autoTask] Stack utilisée: 6349 bytes (77.5%)
```

**Interprétation** : 77.5% d'utilisation = ⚠️ ATTENTION, mais acceptable. Surveiller.

## 🔧 Checklist de Développement

Avant chaque modification de code :

- [ ] Vérifier que les buffers locaux < 512 bytes
- [ ] Utiliser `static` pour les buffers > 256 bytes réutilisés
- [ ] Éviter les `String` Arduino dans les boucles
- [ ] Limiter la profondeur d'appels < 8 niveaux
- [ ] Vérifier l'impact stack des nouvelles bibliothèques
- [ ] Tester avec monitoring stack activé
- [ ] Vérifier les logs après déploiement
- [ ] Documenter les changements de taille de stack

## 📝 Plan d'Action Recommandé

### Priorité 1 (Immédiat)

1. ✅ **Activer le hook de stack overflow** dans `platformio.ini`
2. ✅ **Implémenter le hook** `vApplicationStackOverflowHook()`
3. ✅ **Augmenter AUTOMATION_TASK_STACK_SIZE à 12288** (temporaire)

### Priorité 2 (Court terme)

1. ✅ **Activer monitoring permanent** avec logs conditionnels
2. ✅ **Vérifier et optimiser** les allocations stack dans `automatism_network.cpp`
3. ✅ **Créer script de monitoring** stack usage

### Priorité 3 (Moyen terme)

1. ✅ **Analyser profondeur d'appels** dans automationTask
2. ✅ **Optimiser allocations** JsonDocument (static si possible)
3. ✅ **Refactoriser** opérations SSL si nécessaire

### Priorité 4 (Long terme)

1. ✅ **Audit complet** de toutes les tâches
2. ✅ **Documenter** les contraintes stack par module
3. ✅ **Tests de charge** automatisés

## 🔗 Références

- `DIAGNOSTIC_STACK_OVERFLOW.md` - Diagnostic du problème initial
- `include/config.h` - Configuration des tailles de stack
- `src/app_tasks.cpp` - Implémentation des tâches
- [FreeRTOS Stack Overflow Detection](https://www.freertos.org/Stacks-and-stack-overflow-checking.html)
- [ESP32 Memory Management](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/memory-types.html)

---

**Version** : 1.0  
**Date** : 2026-01-11  
**Auteur** : Guide de prévention stack overflow
