# Rapport d'Analyse Complète des Bugs - FFP5CS

**Date**: 2026-01-17  
**Version analysée**: v11.155+  
**Objectif**: Identifier tous les bugs potentiels sans modifier le code

---

## Résumé Exécutif

Cette analyse a examiné systématiquement le code selon 10 catégories critiques définies dans le plan. **442 utilisations de `String` Arduino** ont été identifiées, ainsi que plusieurs problèmes potentiels de gestion mémoire, réseau, watchdog, et concurrence.

**Statistiques**:
- **442** utilisations de `String` Arduino (risque fragmentation mémoire)
- **40** appels à `esp_task_wdt_reset()` (bonne pratique)
- **44** vérifications `WiFi.status() == WL_CONNECTED` (bonne pratique offline-first)
- **7** timeouts HTTP configurés (bonne pratique)
- **0** utilisations de `delay()` dans les boucles principales (excellent)

---

## 1. GESTION MÉMOIRE (CRITIQUE)

### 1.1 Utilisations de String Arduino (442 occurrences)

**Impact**: CRITIQUE - Fragmentation mémoire sur ESP32 avec 4MB Flash

**Problèmes identifiés**:

#### `src/diagnostics.cpp`
- **Lignes 868-876**: Utilisation de `String` temporaires dans `savePanicInfo()` et `loadPanicInfo()`
  ```cpp
  String tempStr = _stats.panicInfo.exceptionCause;
  g_nvsManager.saveString(NVS_NAMESPACES::LOGS, "diag_panicCause", tempStr);
  ```
  **Recommandation**: Utiliser `strncpy` + buffer statique

#### `src/data_queue.cpp`
- **Lignes 46, 89, 156, 200, 238**: Utilisation de `String` pour les payloads et lignes de fichier
  ```cpp
  bool DataQueue::push(const String& payload) {
    file.println(payload);
  }
  ```
  **Recommandation**: Utiliser `const char*` et buffers statiques

#### `src/web_server.cpp`
- **Lignes 195, 363, 771, 886-888, 1114, 1296-1300, 1351-1352, 1389, 1394, 1409, 1518, 1550, 1707, 1712**: Nombreuses utilisations de `String` dans les handlers HTTP
  ```cpp
  String c = req->getParam("cmd")->value();
  String rel = req->getParam("relay")->value();
  ```
  **Recommandation**: Utiliser `req->getParam()->value()` directement avec `strncpy` si nécessaire

#### `src/web_client.cpp`
- **Ligne 597**: Conversion de buffer en `String`
  ```cpp
  String payload = String(payloadBuffer);
  ```
  **Recommandation**: Utiliser directement `payloadBuffer` avec `const char*`

#### `src/ota_manager.cpp`
- **Lignes 175-178, 218-221, 252-254, 292-294, 308, 327, 820-822, 925-928**: Utilisations multiples de `String` pour URLs et versions
  ```cpp
  String urlStr = v["bin_url"].as<String>();
  String verStr = v["version"].as<String>();
  ```
  **Recommandation**: Utiliser `as<const char*>()` ou buffers statiques

#### `src/mailer.cpp`
- **Lignes multiples**: Utilisation de `String` pour SSID et autres données réseau
  ```cpp
  const char* ssid = WiFi.SSID().c_str();
  ```
  **Note**: Déjà optimisé avec `.c_str()`, mais `WiFi.SSID()` retourne une `String` interne

#### `src/system_boot.cpp`
- **Lignes 20-29, 61-69, 72-81**: Conversions `String` ↔ `char[]` dans les adaptateurs
  ```cpp
  String previousVersionStr(state.previousVersion);
  ```
  **Recommandation**: Éviter les conversions, utiliser directement `char[]`

### 1.2 Allocations dynamiques

**Fichiers avec `new`/`malloc`**:
- `src/web_server.cpp`
- `src/web_client.cpp`
- `src/app.cpp`
- `src/web_routes_status.cpp`

**Analyse**: Les allocations semblent être gérées par les bibliothèques (ESPAsyncWebServer, ArduinoJson). Aucune fuite mémoire évidente détectée dans le code applicatif.

### 1.3 Buffers statiques et débordements

#### `src/diagnostics.cpp`
- **Ligne 187**: Buffer `buf[1024]` pour `vTaskGetRunTimeStats()` - **OK** (taille suffisante)
- **Lignes 214-216**: Buffer `line[120]` pour parsing - **OK**
- **Lignes 236-238**: Buffer `numStr[4]` - **OK**

#### `src/data_queue.cpp`
- **Ligne 112**: `LINE_BUF_SIZE = 512` - **OK** pour JSON

#### `src/mailer.cpp`
- **Lignes 33-36**: Buffers réduits en v11.144 - **OK**
  - `g_systemInfoFooterBuffer[1024]` (réduit de 4096)
  - `g_detailedTimeReportBuffer[512]` (réduit de 2048)
  - `g_lightFooterBuffer[256]`

#### `src/automatism_alert_controller.cpp`
- **Lignes 21, 48, 88, 99, 115, 127**: Buffers `msgBuffer[128]` et `msgBuffer[64]` - **OK**

#### `src/automatism_refill_controller.cpp`
- **Lignes 92-98, 133-140, 183-191, 251-262, 331-338**: Buffers `msg[512]` et `msg[1024]` - **OK**

**Vérification `snprintf`**: Tous les appels vérifient les limites de buffer correctement.

---

## 2. GESTION RÉSEAU ET OFFLINE-FIRST

### 2.1 Timeouts réseau

**✅ BON**: Tous les timeouts HTTP sont configurés:
- `src/web_client.cpp:32`: `_http.setTimeout(GlobalTimeouts::HTTP_MAX_MS)` - **OK**
- `src/ota_manager.cpp`: Timeouts configurés (lignes 316, 333, 632, 1026, 1215) - **OK**

**⚠️ PROBLÈME POTENTIEL**:
- `src/web_server.cpp:68`: Commentaire indique que `setTimeout()` n'est pas disponible dans AsyncWebServer
  ```cpp
  // Note: setTimeout() n'est pas disponible dans cette version d'AsyncWebServer
  ```
  **Recommandation**: Vérifier si une alternative existe ou documenter le risque

### 2.2 Fallbacks offline

**✅ EXCELLENT**: 44 vérifications `WiFi.status() == WL_CONNECTED` avec fallbacks locaux:
- `src/automatism_refill_controller.cpp`: Fallback local pour remplissage (lignes 47-51, 119-123, 166-176, 228-236)
- `src/automatism_sync.cpp`: Fallback local pour synchronisation
- `src/app_tasks.cpp:309-321`: Fallback poll distant si queue vide

**✅ CONFORME**: Le système fonctionne sans WiFi selon les règles du projet.

### 2.3 Gestion WiFi déconnecté

**✅ BON**: Toutes les opérations réseau vérifient le statut WiFi avant d'agir.

---

## 3. WATCHDOG ET BLOCAGES

### 3.1 Reset watchdog

**✅ EXCELLENT**: 40 appels à `esp_task_wdt_reset()` dans:
- `src/app_tasks.cpp`: Dans toutes les tâches (sensorTask, displayTask, webTask, automationTask)
- `src/system_sensors.cpp`: Entre chaque lecture de capteur
- `src/mailer.cpp`: Dans les boucles longues
- `src/automatism_refill_controller.cpp:20`: Après traitement
- `src/automatism_alert_controller.cpp:137`: Après traitement

### 3.2 Blocages > 3 secondes

**✅ BON**: Aucun `delay()` trouvé dans les boucles principales.

**⚠️ ATTENTION**:
- `src/system_sensors.cpp`: Lecture capteurs peut prendre jusqu'à `GLOBAL_TIMEOUT_MS` (30s selon `GlobalTimeouts`)
  - **Mitigation**: Timeout global avec retour anticipé si dépassé
  - **Recommandation**: Réduire `GLOBAL_TIMEOUT_MS` à 5s selon règles projet

- `src/mailer.cpp`: Envoi SMTP peut prendre plusieurs secondes
  - **Mitigation**: Timeout TLS mutex (10s ligne 948)
  - **Recommandation**: Vérifier que le timeout SMTP est également configuré

### 3.3 Timeouts capteurs

**✅ BON**: Tous les capteurs ont des timeouts:
- `src/system_sensors.cpp`: Timeout global `GLOBAL_TIMEOUT_MS` avec vérifications par phase
- `src/app_tasks.cpp:44`: `MAX_SENSOR_TIME_MS = 30000` avec alerte si dépassé

---

## 4. GESTION D'ERREURS ET VALIDATION

### 4.1 Capteurs défaillants

**✅ EXCELLENT**: Tous les capteurs ont des valeurs par défaut:
- `src/app_tasks.cpp:62-82`: Validation indépendante par capteur avec valeurs par défaut
- `src/system_sensors.cpp`: Validation avec `isnan()` et plages valides
- Fallback sur dernières valeurs valides pour ultrasons (lignes 120-122, 159-161)

### 4.2 Validation données

**✅ BON**: Validations présentes:
- Température eau: `5-60°C` (lignes 50-56 `system_sensors.cpp`)
- Température air: Plage définie dans `SensorConfig::AirSensor`
- Humidité: `0-100%` (lignes 77-82 `app_tasks.cpp`)
- Niveaux ultrason: `2-400cm` (lignes 93-98, 110-131 `system_sensors.cpp`)

### 4.3 Pointeurs NULL

**✅ BON**: Vérifications présentes:
- `src/diagnostics.cpp:378`: Vérification `if (!buffer || bufferSize == 0) return;`
- `src/data_queue.cpp:47`: Vérification `if (!_initialized) return false;`
- `src/tls_mutex.cpp`: Vérifications mutex nullptr

---

## 5. CONCURRENCE ET MUTEX

### 5.1 Mutex TLS

**✅ EXCELLENT**: Implémentation robuste dans `include/tls_mutex.h`:
- Vérification heap avant acquisition (ligne 63)
- Vérification light sleep (ligne 56)
- Double vérification après acquisition (lignes 79-90)
- Timeout configurable (ligne 54)

**✅ BON**: Utilisation correcte dans:
- `src/mailer.cpp:948`: Acquisition avec timeout 10s
- `src/app_tasks.cpp:227`: Acquisition avec timeout 3s
- Libération garantie avec RAII ou `release()` explicite

### 5.2 Mutex NVS

**✅ EXCELLENT**: `NVSLockGuard` avec RAII dans `src/nvs_manager.cpp`:
- Timeout par défaut 100ms (ligne 11)
- Libération automatique dans destructeur (lignes 15-19)
- Mutex récursif pour éviter deadlocks (ligne 32)

**⚠️ ATTENTION POTENTIEL**:
- `src/nvs_manager.cpp:354-402`: `saveBool()` n'utilise pas le cache pour éviter écritures inutiles (contrairement à `saveString()` ligne 224-233)
  **Recommandation**: Ajouter vérification cache dans `saveBool()`, `saveInt()`, `saveFloat()`, etc.

### 5.3 Mutex EventLog

**✅ BON**: Timeout 50ms au lieu de `portMAX_DELAY` (lignes 16, 36 `event_log.cpp`) - évite deadlocks

### 5.4 Race conditions potentielles

**⚠️ À VÉRIFIER**:
- `src/data_queue.cpp`: Accès concurrent à `_currentSize` sans mutex
  - **Mitigation**: LittleFS gère la concurrence, mais `_currentSize` peut être désynchronisé
  - **Recommandation**: Ajouter mutex ou utiliser `countEntries()` à chaque fois

---

## 6. NVS (ÉCRITURES ET CORRUPTION)

### 6.1 Écritures inutiles

**✅ BON**: `saveString()` vérifie le cache (lignes 224-233 `nvs_manager.cpp`)

**❌ PROBLÈME**: `saveBool()`, `saveInt()`, `saveFloat()`, `saveULong()` n'ont **PAS** de vérification cache:
- `src/nvs_manager.cpp:354-402`: `saveBool()` écrit toujours
- `src/nvs_manager.cpp`: `saveInt()`, `saveFloat()`, `saveULong()` similaires

**Impact**: Écritures flash inutiles, réduction durée de vie flash

**Recommandation**: Ajouter vérification cache dans toutes les méthodes `save*()`

### 6.2 Corruption NVS

**✅ BON**: Gestion d'erreurs présente:
- Validation clés (ligne 212 `nvs_manager.cpp`)
- Validation valeurs (ligne 218)
- Vérification succès écriture (ligne 240)

### 6.3 Cache NVS

**✅ BON**: Cache implémenté avec:
- Taille max 50 entrées (ligne 62)
- Cleanup périodique (ligne 67: `_cleanupIntervalMs = 3600000UL`)
- Deferred flush (lignes 404-407)

---

## 7. OVERFLOW/UNDERFLOW

### 7.1 Buffers

**✅ EXCELLENT**: Tous les `snprintf` vérifient les limites:
- `src/diagnostics.cpp:377-737`: Vérifications systématiques `written < 0 || (size_t)written >= remaining`
- `src/mailer.cpp`: Vérifications similaires
- `src/automatism_alert_controller.cpp`: Buffers correctement dimensionnés

### 7.2 Arithmétique

**✅ BON**: Gestion wrap-safe pour `millis()`:
- `src/automatism_refill_controller.cpp:205`: `(uint32_t)(nowMs - _pumpStartMs)` avec vérification anomalie (ligne 207)
- `src/system_actuators.cpp:49`: Calcul runtime avec vérification `runtimeStart > 0`

**⚠️ ATTENTION**:
- `src/automatism_refill_controller.cpp:207`: Vérification `elapsedMs / 1000U > 3000000U` semble incorrecte (devrait être `elapsedMs > 3000000000UL`)
  ```cpp
  if ((elapsedMs / 1000U) > 3000000U) {  // Vérifie si > 3e6 secondes
  ```
  **Recommandation**: Corriger la logique ou clarifier l'intention

### 7.3 Indices tableaux

**✅ BON**: Accès sécurisés:
- `src/system_sensors.cpp:256`: `_aquaHistHead = (uint8_t)((_aquaHistHead + 1) % AQUA_HIST_SIZE)`
- `src/data_queue.cpp`: Vérifications limites avant accès

---

## 8. LOGIQUE MÉTIER

### 8.1 Nourrissage

**✅ BON**: Logique vérifiée dans `src/automatism_feeding.cpp`:
- Vérification heures de nourrissage
- Flags de reset quotidien
- Gestion séquentielle pour éviter conflits puissance

### 8.2 Remplissage

**✅ EXCELLENT**: Logique robuste dans `src/automatism_refill_controller.cpp`:
- Sécurité aquarium trop plein (lignes 38-65)
- Vérification réserve basse (lignes 84-103)
- Arrêt forcé après durée max (lignes 203-296)
- Récupération automatique (lignes 360-379)
- Compteur retries avec verrouillage (lignes 241-267)

### 8.3 Chauffage

**✅ BON**: Logique simple dans `src/automatism_alert_controller.cpp:111-135`:
- Seuil température avec hystérésis (2°C)
- État précédent pour éviter oscillations

---

## 9. WEB SERVER

### 9.1 Mémoire requêtes

**⚠️ ATTENTION**: Utilisations de `String` dans les handlers HTTP (voir section 1.1)

**✅ BON**: Pas d'allocations dynamiques évidentes dans les handlers

### 9.2 WebSocket

**✅ BON**: Implémentation simple dans `src/realtime_websocket.cpp`:
- Pas de buffer dynamique
- Gestion activité client

---

## 10. INITIALISATION ET CLEANUP

### 10.1 Ordre d'initialisation

**✅ BON**: Ordre logique dans `src/app.cpp`:
1. Storage (NVS)
2. Réseau (WiFi)
3. Services (time, display)
4. Tâches FreeRTOS

### 10.2 Cleanup

**✅ BON**: Libération ressources:
- `src/web_client.cpp:37-53`: `resetTLSClient()` libère mémoire TLS
- Mutex libérés avec RAII (`NVSLockGuard`, `TLSMutex::release()`)

---

## BUGS CRITIQUES IDENTIFIÉS

### 🔴 CRITIQUE 1: Écritures NVS inutiles
**Fichier**: `src/nvs_manager.cpp`  
**Lignes**: 354-402 (`saveBool()`), et autres `save*()`  
**Problème**: `saveBool()`, `saveInt()`, `saveFloat()`, `saveULong()` n'ont pas de vérification cache avant écriture  
**Impact**: Réduction durée de vie flash, écritures inutiles  
**Priorité**: HAUTE

### 🟡 MOYEN 1: Utilisations excessives de String Arduino
**Fichiers**: Multiple (442 occurrences)  
**Problème**: Fragmentation mémoire potentielle  
**Impact**: Réduction heap disponible, crashes possibles  
**Priorité**: MOYENNE (mitigé par buffers statiques existants)

### 🟡 MOYEN 2: Timeout capteurs trop long
**Fichier**: `src/system_sensors.cpp`  
**Problème**: `GLOBAL_TIMEOUT_MS` peut être 30s (défaut)  
**Impact**: Blocage système si capteur défaillant  
**Priorité**: MOYENNE (mitigé par timeout et retour anticipé)

### 🟢 FAIBLE 1: Vérification anomalie timing
**Fichier**: `src/automatism_refill_controller.cpp:207`  
**Problème**: Logique de vérification `elapsedMs` semble incorrecte  
**Impact**: Détection anomalie peut ne pas fonctionner  
**Priorité**: FAIBLE

### 🟢 FAIBLE 2: Désynchronisation _currentSize
**Fichier**: `src/data_queue.cpp`  
**Problème**: `_currentSize` peut être désynchronisé avec fichier réel  
**Impact**: Comptage incorrect des entrées  
**Priorité**: FAIBLE (mitigé par `countEntries()`)

---

## RECOMMANDATIONS PRIORISÉES

### Priorité HAUTE
1. **Ajouter vérification cache dans toutes les méthodes `save*()` de NVSManager**
   - `saveBool()`, `saveInt()`, `saveFloat()`, `saveULong()`, `saveString()` (déjà fait)
   - Économie flash et amélioration performance

### Priorité MOYENNE
2. **Réduire progressivement les utilisations de `String` Arduino**
   - Commencer par les fonctions appelées fréquemment (`data_queue.cpp`, handlers HTTP)
   - Utiliser `const char*` et buffers statiques

3. **Réduire `GLOBAL_TIMEOUT_MS` à 5s** (selon règles projet)
   - Vérifier que tous les capteurs peuvent répondre en < 5s
   - Ajuster si nécessaire

### Priorité FAIBLE
4. **Corriger vérification anomalie timing** dans `automatism_refill_controller.cpp:207`
5. **Ajouter mutex pour `_currentSize`** dans `data_queue.cpp` ou utiliser `countEntries()` systématiquement

---

## CONCLUSION

Le code est **globalement robuste** et suit bien les principes du projet (offline-first, simplicité, robustesse). Les principaux points d'amélioration concernent:

1. **Gestion mémoire**: Réduction des `String` Arduino (442 occurrences)
2. **NVS**: Ajout vérifications cache dans toutes les méthodes `save*()`
3. **Timeouts**: Ajustement selon règles projet (5s max)

**Score global**: 8/10 - Code de qualité avec quelques optimisations possibles.

---

**Fin du rapport**
