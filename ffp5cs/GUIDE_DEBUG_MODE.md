# Guide d'utilisation de l'outil de debug - Firmware ESP32 FFP5CS

## 📋 Vue d'ensemble

L'outil de debug permet de diagnostiquer des bugs en instrumentant le code avec des logs structurés (format NDJSON) qui sont écrits dans un fichier local. Ces logs sont ensuite analysés pour identifier la cause racine des problèmes.

## ⚡ Résumé rapide (5 minutes)

### 1. Instrumenter le code
```cpp
#include "debug_log.h"

void fonctionSuspecte() {
  // #region agent log
  char jsonBuf[128];
  snprintf(jsonBuf, sizeof(jsonBuf), "{\"heapFree\":%u}", ESP.getFreeHeap());
  DEBUG_LOG_DATA("app.cpp:142", "Function entry", jsonBuf, "A");
  // #endregion agent log
}
```

### 2. Nettoyer le log
```powershell
Remove-Item .cursor\debug.log -ErrorAction SilentlyContinue
```

### 3. Compiler et flasher
```powershell
pio run -e wroom-test
pio run -e wroom-test -t upload
```

### 4. Capturer les logs
Dans un terminal PowerShell :
```powershell
pio device monitor --filter time | Select-String '^\{' | Out-File -Append .cursor\debug.log
```

### 5. Reproduire le bug
Laisser tourner le système jusqu'à reproduction du bug, puis arrêter la capture (Ctrl+C).

### 6. Analyser les logs
L'outil de debug lit automatiquement `.cursor\debug.log` et analyse les hypothèses.

---

**Pour plus de détails, voir les sections suivantes.**

## 🔧 Configuration

### Fichier de logs
- **Chemin**: `.cursor\debug.log` (dans le workspace)
- **Format**: NDJSON (un objet JSON par ligne)
- **Création**: Automatique lors de la première écriture

### Structure d'un log NDJSON
```json
{
  "id": "log_1733456789_abc",
  "timestamp": 1733456789000,
  "location": "app.cpp:142",
  "message": "Description de l'événement",
  "data": {
    "variable1": 42,
    "variable2": "valeur",
    "heapFree": 50000
  },
  "sessionId": "debug-session",
  "runId": "run1",
  "hypothesisId": "A"
}
```

## 🛠️ Étape 1: Utiliser l'utilitaire de logging

Le fichier `include/debug_log.h` est déjà créé et fournit des macros pour instrumenter le code.

### Approche recommandée : Serial (simple et fiable)

**Pour ce firmware ESP32, l'approche la plus simple est d'envoyer les logs NDJSON sur Serial** et de les capturer dans un fichier. Le fichier `debug_log.h` supporte cette approche.

### Utilisation basique

```cpp
#include "debug_log.h"

void someFunction(int param) {
  // #region agent log
  char jsonBuf[128];
  snprintf(jsonBuf, sizeof(jsonBuf), "{\"param\":%d,\"heapFree\":%u}", 
           param, ESP.getFreeHeap());
  DEBUG_LOG_DATA("app.cpp:142", "Function entry", jsonBuf, "A");
  // #endregion agent log
  
  // Code suspect...
}
```

### Deux modes disponibles

Le fichier `debug_log.h` peut fonctionner en deux modes :

1. **Mode Serial** (recommandé) : Envoie les logs NDJSON sur Serial
2. **Mode LittleFS** : Écrit dans `/debug.log` sur LittleFS (nécessite extraction)

**Par défaut, le mode Serial est utilisé** car il est plus simple et ne nécessite pas d'extraction de fichier.

## 📝 Étape 2: Processus de debug complet

### 2.1 Générer des hypothèses

Avant d'instrumenter, identifiez **3-5 hypothèses précises** sur la cause du bug :

**Exemple** : Bug "Le système redémarre après 10 minutes"

Hypothèses :
- **A**: Fragmentation mémoire → heap < 30KB → allocation TLS échoue
- **B**: Watchdog timeout → tâche bloquée > 5 minutes
- **C**: Stack overflow → stack canary déclenché
- **D**: Panic ESP-IDF → assertion échoue dans une bibliothèque
- **E**: Brownout → alimentation instable

### 2.2 Instrumenter le code

Ajoutez **3-8 logs ciblés** pour chaque hypothèse :

```cpp
// #region agent log
void someFunction(int param) {
  char jsonBuf[128];
  snprintf(jsonBuf, sizeof(jsonBuf), "{\"param\":%d,\"heapFree\":%u}", 
           param, ESP.getFreeHeap());
  DEBUG_LOG_DATA("app.cpp:142", "Function entry", jsonBuf, "A");
  
  // Code suspect...
  
  uint32_t heapBefore = ESP.getFreeHeap();
  // Opération critique
  uint32_t heapAfter = ESP.getFreeHeap();
  
  snprintf(jsonBuf, sizeof(jsonBuf), 
           "{\"heapBefore\":%u,\"heapAfter\":%u,\"delta\":%d}",
           heapBefore, heapAfter, (int)(heapBefore - heapAfter));
  DEBUG_LOG_DATA("app.cpp:150", "After critical op", jsonBuf, "A");
  
  // #endregion agent log
}
```

**Règles d'instrumentation** :
- ✅ **3-8 logs** par fonction suspecte
- ✅ **Wrapper avec `// #region agent log` / `// #endregion agent log`** pour auto-fold
- ✅ **Chaque log mappe à au moins une hypothèse** (via `hypothesisId`)
- ✅ **Logs compacts** (éviter allocations dynamiques)
- ✅ **Ne pas logger les secrets** (tokens, passwords, etc.)

### 2.3 Nettoyer le fichier de log avant chaque run

Avant de demander à l'utilisateur de reproduire le bug, **supprimez le fichier de log** :

**Mode Serial (recommandé)** :
- Utilisez l'outil `delete_file` pour supprimer `.cursor\debug.log` avant chaque run
- OU exécutez : `Remove-Item .cursor\debug.log -ErrorAction SilentlyContinue`

**Mode LittleFS** :
```cpp
// Dans votre code de setup() ou avant le test
DebugLog::clear(); // Nettoie /debug.log sur LittleFS
```

### 2.4 Demander la reproduction

Fournissez des instructions claires dans un bloc `<reproduction_steps>` :

**Mode Serial (recommandé)** :
```xml
<reproduction_steps>
1. Compiler le firmware avec l'instrumentation (pio run -e wroom-test)
2. Flasher sur l'ESP32 (pio run -e wroom-test -t upload)
3. Dans un terminal PowerShell, exécuter: pio device monitor --filter time | Select-String '^\{' | Out-File -Append .cursor\debug.log
4. Laisser tourner pendant 15 minutes
5. Observer le redémarrage
6. Arrêter la capture (Ctrl+C dans le terminal)
7. Cliquer sur "Proceed" une fois le bug reproduit
</reproduction_steps>
```

**Alternative avec script** :
```xml
<reproduction_steps>
1. Compiler le firmware avec l'instrumentation (pio run -e wroom-test)
2. Flasher sur l'ESP32 (pio run -e wroom-test -t upload)
3. Exécuter le script: .\capture_debug_logs.ps1 900 (15 minutes)
4. Dans un autre terminal, exécuter: pio device monitor --filter time | Select-String '^\{' | Out-File -Append .cursor\debug.log
5. Laisser tourner jusqu'à reproduction du bug
6. Cliquer sur "Proceed" une fois le bug reproduit
</reproduction_steps>
```

### 2.5 Analyser les logs

Après reproduction, lisez `.cursor\debug.log` et évaluez chaque hypothèse :

- **CONFIRMÉ** : Logs montrent la cause (ex: `heapFree: 25000` < 30000)
- **REJETÉ** : Logs contredisent l'hypothèse (ex: heap stable à 60KB)
- **INCONCLUSIF** : Logs insuffisants, besoin de plus d'instrumentation

**Exemple d'analyse** :
```
Log ligne 15: {"heapFree":52000,"message":"Function entry"} → Hypothèse A REJETÉE (heap OK)
Log ligne 23: {"heapFree":28000,"message":"After TLS alloc"} → Hypothèse A CONFIRMÉE (heap < 30KB)
```

### 2.6 Implémenter le fix

Une fois la cause identifiée avec **100% de confiance** (preuve dans les logs), implémentez le fix **SANS retirer les logs** :

```cpp
// Fix basé sur les logs
if (ESP.getFreeHeap() < TLS_MIN_HEAP_BYTES) {
  // Libérer de la mémoire avant allocation TLS
  cleanupTemporaryBuffers();
}
```

### 2.7 Vérifier avec logs

Demandez une **deuxième reproduction** avec le fix et comparez les logs avant/après :

- **Avant fix** : `{"heapFree":28000}` → crash
- **Après fix** : `{"heapFree":55000}` → fonctionnement normal

### 2.8 Retirer l'instrumentation

**UNIQUEMENT** après confirmation du succès par l'utilisateur et preuve dans les logs, retirez les logs de debug.

## 🎯 Exemple complet : Debug d'un crash mémoire

### Problème
Le système redémarre après 10 minutes d'utilisation.

### Hypothèses
- **A**: Fragmentation mémoire (heap minimum < 30KB)
- **B**: Allocation TLS échoue (heap libre < 52KB)
- **C**: Stack overflow dans une tâche FreeRTOS

### Instrumentation

```cpp
#include "debug_log.h"

void Automatism::update() {
  // #region agent log
  char jsonBuf[256];
  uint32_t heapFree = ESP.getFreeHeap();
  uint32_t heapMin = ESP.getMinFreeHeap();
  snprintf(jsonBuf, sizeof(jsonBuf), 
           "{\"heapFree\":%u,\"heapMin\":%u,\"stackHighWater\":%u}",
           heapFree, heapMin, uxTaskGetStackHighWaterMark(nullptr));
  DEBUG_LOG_DATA("automatism.cpp:45", "Automatism update entry", jsonBuf, "A");
  DEBUG_LOG_DATA("automatism.cpp:45", "Automatism update entry", jsonBuf, "B");
  // #endregion agent log
  
  // Code existant...
  
  if (needsTLSConnection()) {
    // #region agent log
    uint32_t heapBeforeTLS = ESP.getFreeHeap();
    snprintf(jsonBuf, sizeof(jsonBuf), "{\"heapBeforeTLS\":%u}", heapBeforeTLS);
    DEBUG_LOG_DATA("automatism.cpp:120", "Before TLS connection", jsonBuf, "B");
    // #endregion agent log
    
    bool success = connectTLS();
    
    // #region agent log
    uint32_t heapAfterTLS = ESP.getFreeHeap();
    snprintf(jsonBuf, sizeof(jsonBuf), 
             "{\"success\":%d,\"heapBefore\":%u,\"heapAfter\":%u}",
             success ? 1 : 0, heapBeforeTLS, heapAfterTLS);
    DEBUG_LOG_DATA("automatism.cpp:125", "After TLS connection", jsonBuf, "B");
    // #endregion agent log
  }
}
```

### Analyse des logs

```json
{"timestamp":600000,"location":"automatism.cpp:45","message":"Automatism update entry","data":{"heapFree":60000,"heapMin":55000},"hypothesisId":"A"}
{"timestamp":600000,"location":"automatism.cpp:120","message":"Before TLS connection","data":{"heapBeforeTLS":45000},"hypothesisId":"B"}
{"timestamp":600001,"location":"automatism.cpp:125","message":"After TLS connection","data":{"success":0,"heapBefore":45000,"heapAfter":28000},"hypothesisId":"B"}
```

**Conclusion** : Hypothèse **B CONFIRMÉE** - TLS échoue car heap insuffisant (28KB < 52KB requis).

### Fix

```cpp
if (needsTLSConnection()) {
  // Libérer mémoire avant TLS si nécessaire
  if (ESP.getFreeHeap() < TLS_MIN_HEAP_BYTES) {
    cleanupTemporaryBuffers();
    // Attendre garbage collection
    delay(100);
  }
  
  if (ESP.getFreeHeap() >= TLS_MIN_HEAP_BYTES) {
    bool success = connectTLS();
  } else {
    LOG_WARN("TLS skipped: heap insuffisant (%u < %u)", 
             ESP.getFreeHeap(), TLS_MIN_HEAP_BYTES);
  }
}
```

## ⚠️ Contraintes spécifiques ESP32

### Mémoire limitée
- **Heap libre critique** : < 30KB → risque de crash
- **TLS minimum** : 52KB requis pour connexions HTTPS/SMTP
- **Éviter allocations dynamiques** dans les logs (utiliser buffers statiques)

### Watchdog
- **Timeout** : 5 minutes (300s) configuré dans `app.cpp`
- **Ne pas bloquer** > 3 secondes dans `loop()`
- **Appeler `esp_task_wdt_reset()`** dans les boucles longues

### Système de fichiers
- **LittleFS** : Monté automatiquement au boot
- **Si FS non disponible** : Logs ignorés silencieusement (ne pas bloquer)
- **Performance** : Écritures séquentielles (append) sont rapides

### Multi-tâches FreeRTOS
- **Thread-safety** : LittleFS est thread-safe (mutex interne)
- **Pas de logs depuis ISR** : Utiliser uniquement depuis tâches

## 🔍 Bonnes pratiques

1. **Logs ciblés** : Ne logger que ce qui est nécessaire pour tester les hypothèses
2. **Format compact** : JSON minimal pour économiser l'espace disque
3. **Pas de secrets** : Jamais de tokens, passwords, clés API
4. **Performance** : Logs asynchrones si possible (ne pas bloquer le système)
5. **Nettoyage** : Supprimer le fichier de log avant chaque run
6. **Vérification** : Toujours comparer logs avant/après fix

## 📊 Structure recommandée des logs

```json
{
  "timestamp": 600000,
  "location": "fichier.cpp:ligne",
  "message": "Description brève",
  "data": {
    "variable1": 42,
    "variable2": "valeur",
    "heapFree": 50000,
    "heapMin": 45000
  },
  "sessionId": "debug-session",
  "runId": "run1",
  "hypothesisId": "A"
}
```

## 🚫 À éviter

- ❌ Logs trop verbeux (surcharge du système)
- ❌ Allocations dynamiques dans les logs (`new`, `malloc`, `String`)
- ❌ Logs depuis ISR (interrupt service routines)
- ❌ Blocage du système pour écrire les logs
- ❌ Retirer les logs avant vérification du fix
- ❌ Fixer sans preuve dans les logs

## ✅ Checklist de debug

- [ ] 3-5 hypothèses générées
- [ ] Code instrumenté (3-8 logs par hypothèse)
- [ ] Fichier de log nettoyé avant run
- [ ] Instructions de reproduction fournies
- [ ] Logs analysés (CONFIRMÉ/REJETÉ/INCONCLUSIF)
- [ ] Fix implémenté avec preuve log
- [ ] Vérification avec logs avant/après
- [ ] Instrumentation retirée après confirmation

---

**Note** : Ce guide est spécifique au firmware ESP32 FFP5CS. Pour d'autres projets, adaptez la fonction `DEBUG_LOG` selon le système de fichiers disponible.
