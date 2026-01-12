# 🔍 Diagnostic du Stack Overflow dans autoTask

## 📊 Situation Actuelle

- **Tâche concernée** : `autoTask` (automationTask)
- **Stack configuré** : 8192 bytes (8 KB)
- **Erreur** : Stack canary watchpoint triggered
- **Contexte** : Crash après ~2 min 30 s pendant un handshake SSL/TLS

## 🔧 Méthodes de Diagnostic

### 1. Vérifier l'utilisation actuelle de la stack

Le projet utilise déjà `uxTaskGetStackHighWaterMark()` dans `diagnostics.cpp`. Voici comment l'utiliser pour `autoTask` :

#### Option A : Ajouter des logs dans automationTask

Ajoutez ce code dans `src/app_tasks.cpp` dans la fonction `automationTask()` :

```cpp
void automationTask(void* pv) {
  // ... code existant ...
  
  static unsigned long lastStackCheck = 0;
  const unsigned long stackCheckInterval = 30000; // Toutes les 30 secondes
  
  for (;;) {
    esp_task_wdt_reset();
    
    // Vérification de la stack périodique
    unsigned long now = millis();
    if (now - lastStackCheck > stackCheckInterval) {
      UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(nullptr);
      Serial.printf("[autoTask] Stack High Water Mark: %u bytes (sur %u bytes configurés)\n", 
                    stackHighWaterMark, 
                    TaskConfig::AUTOMATION_TASK_STACK_SIZE);
      Serial.printf("[autoTask] Stack utilisée: %u bytes (%.1f%%)\n",
                    TaskConfig::AUTOMATION_TASK_STACK_SIZE - stackHighWaterMark,
                    (1.0 - (float)stackHighWaterMark / TaskConfig::AUTOMATION_TASK_STACK_SIZE) * 100.0);
      lastStackCheck = now;
    }
    
    // ... reste du code ...
  }
}
```

#### Option B : Vérifier depuis diagnostics.cpp

Le code existe déjà mais vérifiez qu'il utilise le handle de la tâche :

```cpp
// Dans diagnostics.cpp, vérifier si on peut obtenir le HWM de autoTask
if (g_autoTaskHandle != nullptr) {
  UBaseType_t autoTaskHWM = uxTaskGetStackHighWaterMark(g_autoTaskHandle);
  Serial.printf("[Diagnostics] autoTask Stack HWM: %u bytes\n", autoTaskHWM);
}
```

### 2. Analyser le backtrace du crash

Le backtrace du crash peut indiquer quelle fonction consomme le plus de stack. Regardez dans le log :

```
Backtrace: 0x401e0a0d:0x3ffea3f0 0x401e15ae:0x3ffea710 ...
```

Pour décoder ce backtrace, vous pouvez utiliser :

```bash
# Décompiler les adresses (nécessite addr2line ou similaire)
xtensa-esp32-elf-addr2line -pfiaC -e .pio/build/wroom-test/firmware.elf 0x401e0a0d
```

### 3. Identifier les zones gourmandes en stack

Dans `automationTask`, les opérations suivantes peuvent consommer beaucoup de stack :

#### a) Connexions HTTPS (SSL/TLS)
- Le handshake SSL/TLS consomme beaucoup de stack
- Vérifiez dans `automatism.update()` → `fetchRemoteState()` → connexions HTTPS

#### b) Parsing JSON
- Les documents ArduinoJson peuvent consommer de la stack si alloués localement
- Vérifiez les `JsonDocument` créés dans `automationTask`

#### c) Appels de fonctions récursifs ou profondément imbriqués
- Vérifiez la profondeur d'appels dans `automatism.update()` et ses sous-fonctions

### 4. Solutions temporaires pour tester

#### Augmenter la stack (solution rapide)

Dans `include/config.h`, ligne 439 :

```cpp
// AVANT
constexpr uint32_t AUTOMATION_TASK_STACK_SIZE = 8192;

// APRÈS (test)
constexpr uint32_t AUTOMATION_TASK_STACK_SIZE = 12288; // +50%
```

**⚠️ Attention** : Augmenter la stack réduit la mémoire heap disponible !

#### Réduire la taille des buffers locaux

Si des buffers locaux sont alloués dans `automationTask`, les réduire ou les déplacer en static :

```cpp
// ÉVITER (allocation sur stack)
void automationTask(void* pv) {
  char largeBuffer[2048];  // ❌ Consomme la stack
  JsonDocument doc(4096);  // ❌ Consomme la stack
}

// PRÉFÉRER (allocation statique ou heap)
void automationTask(void* pv) {
  static char largeBuffer[2048];  // ✅ Alloué une seule fois
  // OU utiliser heap avec new/malloc si nécessaire
}
```

### 5. Analyse approfondie avec FreeRTOS

#### Activer le hook de stack overflow

Dans `platformio.ini`, ajoutez :

```ini
build_flags = 
    ${env.build_flags}
    -DconfigCHECK_FOR_STACK_OVERFLOW=2
```

Puis implémentez le hook dans votre code :

```cpp
extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  Serial.printf("⚠️ STACK OVERFLOW dans tâche: %s\n", pcTaskName);
  Serial.printf("⚠️ Task handle: %p\n", xTask);
  // Le système va reboot automatiquement après
}
```

### 6. Script de monitoring dédié

Créez un script PowerShell pour surveiller les logs de stack :

```powershell
# monitor_stack_usage.ps1
$logFile = "monitor_wroom_test_stack.log"
$pattern = "Stack High Water Mark|Stack utilisée|autoTask.*Stack"

pio device monitor --port COM4 --baud 115200 | Tee-Object -FilePath $logFile | 
    Select-String -Pattern $pattern -Context 2,2
```

## 📈 Interprétation des Résultats

### Stack HWM normal
- **> 2048 bytes libres** (75% utilisés) : ✅ OK mais à surveiller
- **> 1024 bytes libres** (87.5% utilisés) : ⚠️ Proche de la limite
- **< 1024 bytes libres** (87.5%+ utilisés) : ❌ CRITIQUE - risque d'overflow

### Recommandations selon l'utilisation

| Utilisation Stack | Action Recommandée |
|-------------------|-------------------|
| < 70% | ✅ OK, pas d'action |
| 70-85% | ⚠️ Surveiller, optimiser si possible |
| 85-95% | ⚠️⚠️ Augmenter stack ou optimiser code |
| > 95% | ❌ CRITIQUE - Augmenter stack immédiatement |

## 🎯 Plan d'Action Recommandé

1. **Immédiat** : Ajouter les logs de stack dans `automationTask` (Option A ci-dessus)
2. **Court terme** : Augmenter temporairement la stack à 12288 bytes pour stabiliser
3. **Moyen terme** : Analyser le code pour identifier les zones gourmandes
4. **Long terme** : Optimiser le code (réduire buffers locaux, optimiser JSON, etc.)

## 📝 Notes

- Le crash se produit pendant un handshake SSL/TLS, ce qui est une opération très gourmande en stack
- La stack peut être consommée de manière cumulative (variables locales qui s'empilent)
- Les fonctions récursives ou avec beaucoup de paramètres consomment plus de stack
