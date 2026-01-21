# Rapport d'analyse des incohérences du code - FFP5CS

**Date d'analyse** : 2026-01-17  
**Version analysée** : v11.155  
**Analyseur** : Audit automatique complet

---

## Résumé exécutif

**Total d'incohérences identifiées** : 23  
**Criticité** :
- 🔴 **Critique** : 3
- 🟡 **Important** : 12
- 🟢 **Mineur** : 8

---

## 1. Incohérences de version

### 🔴 CRITIQUE : Version non documentée dans VERSION.md

**Fichiers concernés** :
- `include/config.h` ligne 15 : `VERSION = "11.155"`
- `VERSION.md` : Dernière version documentée = 11.138

**Description** : La version définie dans `config.h` (11.155) n'est pas documentée dans `VERSION.md`. Les versions 11.139 à 11.155 manquent dans l'historique.

**Impact** : Difficulté à tracer l'historique des changements, risque de confusion lors du debugging.

**Recommandation** : Ajouter les entrées manquantes dans `VERSION.md` pour toutes les versions entre 11.138 et 11.155.

---

### 🟡 IMPORTANT : Références hardcodées de versions dans les scripts

**Fichiers concernés** :
- `flash_wroom_test.ps1` ligne 1, 4, 33, 84, 86 : Version v11.129 hardcodée
- `flash_and_monitor_30min_wroom_test.ps1` ligne 2, 6, 185 : Version v11.124 hardcodée
- `flash_and_monitor_until_reboot.ps1` ligne 2, 12, 300 : Version v11.125 hardcodée
- `monitor_90s_v11.122.ps1`, `monitor_90s_v11.123.ps1`, `monitor_90s_v11.123_nofooter.ps1` : Versions obsolètes

**Description** : Plusieurs scripts PowerShell contiennent des versions hardcodées qui ne correspondent plus à la version actuelle.

**Impact** : Confusion lors de l'utilisation des scripts, risque d'utiliser des scripts obsolètes.

**Recommandation** : Utiliser `ProjectConfig::VERSION` ou une variable d'environnement pour les scripts, ou supprimer les scripts obsolètes.

---

## 2. Incohérences de configuration

### 🔴 CRITIQUE : Constantes EPOCH dupliquées

**Fichiers concernés** :
- `include/config.h` ligne 67-68 : `SystemConfig::EPOCH_MIN_VALID = 1704067200`
- `include/config.h` ligne 412-413 : `SleepConfig::EPOCH_MIN_VALID = 1704067200`

**Description** : Les constantes `EPOCH_MIN_VALID` et `EPOCH_MAX_VALID` sont définies deux fois dans deux namespaces différents (`SystemConfig` et `SleepConfig`) avec les mêmes valeurs.

**Impact** : Duplication de code, risque d'incohérence si une valeur est modifiée sans modifier l'autre.

**Recommandation** : Définir ces constantes une seule fois dans un namespace commun (par exemple `SystemConfig`) et les référencer depuis `SleepConfig` ou utiliser un alias.

**Code actuel** :
```cpp
namespace SystemConfig {
    constexpr time_t EPOCH_MIN_VALID = 1704067200; // 2024-01-01
    constexpr time_t EPOCH_MAX_VALID = 2524608000; // 2050-01-01
}

namespace SleepConfig {
    constexpr time_t EPOCH_MIN_VALID = 1704067200; // 2024-01-01
    constexpr time_t EPOCH_MAX_VALID = 2524608000; // 2050-01-01
}
```

**Code recommandé** :
```cpp
namespace SystemConfig {
    constexpr time_t EPOCH_MIN_VALID = 1704067200; // 2024-01-01
    constexpr time_t EPOCH_MAX_VALID = 2524608000; // 2050-01-01
}

namespace SleepConfig {
    // Alias vers SystemConfig pour éviter duplication
    constexpr time_t EPOCH_MIN_VALID = SystemConfig::EPOCH_MIN_VALID;
    constexpr time_t EPOCH_MAX_VALID = SystemConfig::EPOCH_MAX_VALID;
}
```

---

### 🟡 IMPORTANT : Incohérence validation température air

**Fichiers concernés** :
- `src/system_sensors.cpp` ligne 71 : Utilise `<=` et `>=` au lieu de `<` et `>`
- `include/config.h` ligne 266-267 : Définit `TEMP_MIN = 3.0f` et `TEMP_MAX = 50.0f`

**Description** : La validation utilise des opérateurs inclusifs (`<=`, `>=`) alors que les constantes définissent des limites exclusives. Cela signifie que les valeurs exactement égales à `TEMP_MIN` ou `TEMP_MAX` seront rejetées, ce qui peut être intentionnel mais incohérent avec les autres validations.

**Impact** : Comportement potentiellement inattendu si une valeur exactement égale à la limite est mesurée.

**Recommandation** : Utiliser `<` et `>` pour être cohérent avec les autres validations, ou documenter pourquoi les limites sont exclusives.

**Code actuel** :
```cpp
if (isnan(val) || val <= SensorConfig::AirSensor::TEMP_MIN || val >= SensorConfig::AirSensor::TEMP_MAX) {
```

**Code recommandé** :
```cpp
if (isnan(val) || val < SensorConfig::AirSensor::TEMP_MIN || val > SensorConfig::AirSensor::TEMP_MAX) {
```

---

### 🟡 IMPORTANT : Seuils serveur distant non synchronisés

**Fichiers concernés** :
- `include/config.h` : Seuils ESP32
- `ffp3/src/Service/SensorDataService.php` ligne 34-53 : Seuils serveur

**Description** : Les seuils de validation côté serveur PHP diffèrent légèrement des seuils ESP32 :
- **TempEau** : Serveur min=3.0, max=50.0 | ESP32 min=5.0, max=60.0
- **TempAir** : Serveur min=3.0 | ESP32 min=3.0, max=50.0
- **EauAquarium** : Serveur min=4.0, max=70.0 | ESP32 non défini explicitement

**Impact** : Le serveur peut accepter des valeurs que l'ESP32 rejette, ou vice versa, créant une incohérence dans les données.

**Recommandation** : Synchroniser les seuils entre ESP32 et serveur, ou documenter pourquoi ils diffèrent.

---

### 🟢 MINEUR : Clés NVS avec préfixes différents

**Fichiers concernés** :
- `src/gpio_parser.cpp` ligne 291 : Utilise `"gpio_"` comme préfixe
- `src/config.cpp` : Utilise des clés sans préfixe (`"bouffe_matin"`, etc.)

**Description** : Certaines clés NVS utilisent le préfixe `"gpio_"` alors que d'autres non. Pas d'incohérence fonctionnelle mais manque de cohérence.

**Impact** : Faible, mais rend le debugging plus difficile.

**Recommandation** : Standardiser l'utilisation des préfixes ou documenter la convention.

---

## 3. Incohérences de pins/GPIO

### 🟡 IMPORTANT : GPIO mapping serveur non synchronisé

**Fichiers concernés** :
- `include/gpio_mapping.h` : Mapping ESP32
- `ffp3/src/Controller/OutputController.php` ligne 54-67 : Mapping serveur

**Description** : Le mapping serveur utilise des clés différentes (`'taThr'` au lieu de `'tankThreshold'`, `'bouffeMat'` au lieu de `'feedMorn'`). Le serveur liste les GPIOs 2, 15, 16, 18 mais le mapping ESP32 utilise `Pins::RADIATEURS` qui peut être différent selon la carte.

**Impact** : Risque de désynchronisation entre les commandes serveur et l'ESP32.

**Recommandation** : Vérifier que le mapping serveur correspond exactement au mapping ESP32, notamment pour les pins physiques qui varient selon la carte (WROOM vs S3).

---

### 🟢 MINEUR : Documentation GPIO mapping obsolète

**Fichiers concernés** :
- `include/gpio_mapping.h` ligne 12 : Version 11.68 mentionnée
- Code actuel : Version 11.155

**Description** : La version dans le commentaire du fichier est obsolète.

**Impact** : Faible, confusion mineure.

**Recommandation** : Mettre à jour le commentaire de version.

---

## 4. Incohérences de gestion mémoire

### 🟡 IMPORTANT : Seuils mémoire non utilisés de manière cohérente

**Fichiers concernés** :
- `include/config.h` ligne 235-236 : `LOW_MEMORY_THRESHOLD_BYTES = 8000`, `CRITICAL_MEMORY_THRESHOLD_BYTES = 15000`
- `src/web_server_context.cpp` ligne 36 : Utilise `ESP.getFreeHeap()` directement sans référence aux seuils
- `src/ota_manager.cpp` ligne 552 : Utilise `BufferConfig::CRITICAL_MEMORY_THRESHOLD_BYTES` (cohérent)

**Description** : Certains endroits vérifient le heap libre mais n'utilisent pas les constantes définies dans `BufferConfig`, utilisant des valeurs hardcodées ou des seuils différents.

**Impact** : Incohérence dans la gestion mémoire, certains modules peuvent réagir différemment aux mêmes conditions de mémoire.

**Recommandation** : Utiliser systématiquement `BufferConfig::LOW_MEMORY_THRESHOLD_BYTES` et `BufferConfig::CRITICAL_MEMORY_THRESHOLD_BYTES` partout où le heap est vérifié.

---

### 🟢 MINEUR : Stack sizes non vérifiés à l'exécution

**Fichiers concernés** :
- `include/config.h` ligne 472-486 : Définit les tailles de stack
- `src/app_tasks.cpp` : Crée les tâches avec ces tailles

**Description** : Les tailles de stack sont définies mais jamais vérifiées à l'exécution pour détecter les débordements potentiels.

**Impact** : Faible, mais un monitoring des watermarks de stack serait bénéfique.

**Recommandation** : Ajouter un monitoring périodique des watermarks de stack (déjà partiellement fait dans `TaskMonitor`).

---

## 5. Incohérences offline-first

### 🟡 IMPORTANT : Vérifications WiFi manquantes dans certaines fonctions

**Fichiers concernés** :
- `src/mailer.cpp` ligne 82, 183, 238 : Utilise `WiFi.isConnected()` mais certaines fonctions peuvent être appelées sans vérification
- `src/web_client.cpp` : Vérifie `WiFi.status() != WL_CONNECTED` mais certaines fonctions peuvent échouer silencieusement

**Description** : Toutes les fonctions réseau vérifient le WiFi, mais certaines pourraient bénéficier de fallbacks plus explicites.

**Impact** : Fonctionnement correct en mode offline, mais certains cas d'erreur pourraient être mieux gérés.

**Recommandation** : Vérifier que tous les appels réseau ont des fallbacks locaux explicites (déjà bien implémenté dans la plupart des cas).

---

### 🟢 MINEUR : Timeout OTA justifié mais documenté

**Fichiers concernés** :
- `include/config.h` ligne 159 : `OTA_TIMEOUT_MS = 30000` (30s)
- Commentaire ligne 158 : Justification présente

**Description** : Le timeout OTA de 30s est justifié dans le code, ce qui est correct. Pas d'incohérence, juste une note.

**Impact** : Aucun, c'est correct.

---

## 6. Incohérences de timeouts/delays

### 🟡 IMPORTANT : Delay bloquant dans setup()

**Fichiers concernés** :
- `src/app.cpp` ligne 77, 107, 115 : Utilise `delay()` dans `setup()`

**Description** : `delay()` est utilisé dans `setup()` ce qui est acceptable, mais ligne 115 il y a un `delay(1000)` conditionnel pour debug qui pourrait être optimisé.

**Impact** : Faible, mais conforme aux règles du projet (delays acceptables dans setup).

**Recommandation** : Vérifier que tous les delays dans `loop()` utilisent `vTaskDelay()` (déjà fait ligne 358).

---

### 🟢 MINEUR : Timeout HTTP utilisé de manière incohérente

**Fichiers concernés** :
- `include/config.h` ligne 156 : `HTTP_TIMEOUT_MS = 5000`
- `src/web_client.cpp` ligne 32 : Utilise `GlobalTimeouts::HTTP_MAX_MS` (cohérent)
- `src/ota_manager.cpp` ligne 316, 333, 632 : Utilise `OTAConfig::HTTP_TIMEOUT` (différent)

**Description** : Le timeout HTTP standard est défini dans `NetworkConfig::HTTP_TIMEOUT_MS` mais OTA utilise `OTAConfig::HTTP_TIMEOUT` qui peut être différent.

**Impact** : Faible, OTA nécessite un timeout plus long, ce qui est justifié.

**Recommandation** : Documenter pourquoi OTA utilise un timeout différent, ou utiliser une constante dérivée.

---

## 7. Incohérences de validation de données

### 🔴 CRITIQUE : Bug syntaxe dans automatism_feeding_schedule.cpp

**Fichiers concernés** :
- `src/automatism/automatism_feeding_schedule.cpp` ligne 42-48

**Description** : Ligne 42, il manque une accolade ouvrante `{` après `if (shouldFeedMorning)`. Le code actuel a :
```cpp
if (shouldFeedMorning)
    Serial.printf(...);
    performFeeding(...);
```

Cela signifie que seul `Serial.printf()` est dans le `if`, et `performFeeding()` s'exécute toujours.

**Impact** : **CRITIQUE** - Le nourrissage matin s'exécute toujours, pas seulement à l'heure programmée.

**Recommandation** : Ajouter l'accolade ouvrante manquante :
```cpp
if (shouldFeedMorning) {
    Serial.printf(...);
    performFeeding(...);
    ...
}
```

---

### 🟡 IMPORTANT : Pas de validation des heures de nourrissage (0-23)

**Fichiers concernés** :
- `src/automatism/automatism_feeding_schedule.cpp` ligne 63-65 : `shouldFeedNow()` ne valide pas que `scheduleHour` est entre 0 et 23
- `src/gpio_parser.cpp` : Parse les heures depuis JSON sans validation

**Description** : Les heures de nourrissage (matin, midi, soir) sont acceptées sans validation de plage. Une valeur invalide (ex: 25) pourrait causer un comportement inattendu.

**Impact** : Comportement imprévisible si une valeur invalide est reçue du serveur ou stockée en NVS.

**Recommandation** : Ajouter une validation dans `GPIOParser::parseInt()` ou dans `ConfigManager::setBouffeMatin/Midi/Soir()` pour s'assurer que les heures sont entre 0 et 23.

---

### 🟡 IMPORTANT : Validation distances ultrason non utilisée partout

**Fichiers concernés** :
- `include/config.h` ligne 278-279 : `MIN_DISTANCE_CM = 2`, `MAX_DISTANCE_CM = 400`
- `src/sensors.cpp` : Utilise des validations hardcodées (ligne 41, 86, 229)

**Description** : Les constantes de validation ultrason sont définies mais le code utilise parfois des valeurs hardcodées ou des validations différentes.

**Impact** : Incohérence potentielle dans la validation des mesures ultrason.

**Recommandation** : Utiliser systématiquement `SensorConfig::Ultrasonic::MIN_DISTANCE_CM` et `MAX_DISTANCE_CM` partout où les distances sont validées.

---

## 8. Incohérences de dépendances réseau

### 🟡 IMPORTANT : Vérifications WebSocket manquantes

**Fichiers concernés** :
- `src/web_server.cpp` ligne 126, 207, 262, 313, 322, 331, 357 : Appels à `g_realtimeWebSocket.broadcastNow()` sans vérification `connectedClients() > 0`
- `include/realtime_websocket.h` : La méthode `broadcastNow()` devrait vérifier en interne

**Description** : Certains appels à `broadcastNow()` ne vérifient pas s'il y a des clients connectés avant d'envoyer, ce qui peut causer des allocations mémoire inutiles.

**Impact** : Perte de performance et fragmentation mémoire si des broadcasts sont envoyés sans clients.

**Recommandation** : Vérifier `connectedClients() > 0` avant chaque appel à `broadcastNow()`, ou ajouter cette vérification dans la méthode `broadcastNow()` elle-même.

---

### 🟢 MINEUR : API key utilisée correctement

**Fichiers concernés** :
- `src/automatism/automatism_sync.cpp` ligne 234 : Utilise `ApiConfig::API_KEY` (correct)
- `src/gpio_notifier.cpp` ligne 17 : Utilise `ApiConfig::API_KEY` (correct)

**Description** : L'API key est utilisée de manière cohérente via `ApiConfig::API_KEY`. Pas d'incohérence.

**Impact** : Aucun.

---

## 9. Incohérences de nommage/conventions

### 🟡 IMPORTANT : Utilisation excessive de String au lieu de char[]

**Fichiers concernés** :
- `src/web_server.cpp` : Nombreuses utilisations de `String` (lignes 191, 363, 411, 480, 494, 581, etc.)
- `src/web_client.cpp` : Utilisations de `String` (lignes 82-84, 176, 212, 359-361, etc.)
- `src/ota_manager.cpp` : Nombreuses utilisations de `String` pour les logs

**Description** : Selon `.cursorrules`, on devrait préférer `char[]` aux `String` Arduino pour éviter la fragmentation mémoire. Le code utilise encore beaucoup de `String`, notamment dans les logs et les manipulations de texte.

**Impact** : Fragmentation mémoire potentielle, surtout dans les boucles ou les fonctions fréquemment appelées.

**Recommandation** : Remplacer progressivement les `String` par des `char[]` avec `snprintf()`, en priorisant les fonctions fréquemment appelées et les boucles.

**Exemples prioritaires** :
- `src/web_server.cpp` ligne 886-888 : Construction de strings pour email
- `src/web_client.cpp` ligne 359-361 : Formatage de valeurs NaN
- `src/ota_manager.cpp` : Logs avec concaténation de String

---

### 🟢 MINEUR : Variables globales bien préfixées

**Fichiers concernés** :
- `src/app.cpp` ligne 65-71 : Variables globales utilisent le préfixe `g_` (correct)

**Description** : Les variables globales suivent la convention de nommage avec préfixe `g_`. Pas d'incohérence.

**Impact** : Aucun.

---

## 10. Incohérences de types de données

### 🟡 IMPORTANT : Types NVS parfois incohérents

**Fichiers concernés** :
- `src/diagnostics.cpp` ligne 97-98 : Sauvegarde `minFreeHeap` (uint32_t) avec `saveInt()` (int)
- `src/diagnostics.cpp` ligne 150 : Charge `minFreeHeap` avec `loadInt()` puis cast en `(int&)`

**Description** : `minFreeHeap` est de type `uint32_t` mais est sauvegardé/chargé avec des méthodes `Int` (signé). Cela fonctionne mais peut causer des problèmes si la valeur dépasse `INT_MAX` (2,147,483,647).

**Impact** : Faible risque car les valeurs de heap sont généralement < 500KB, mais théoriquement possible.

**Recommandation** : Utiliser `saveULong()` et `loadULong()` pour les valeurs `uint32_t`, ou créer des méthodes `saveUInt()` et `loadUInt()` si nécessaire.

---

### 🟢 MINEUR : snprintf utilisé correctement

**Fichiers concernés** :
- `src/web_client.cpp`, `src/mailer.cpp`, `src/web_server.cpp` : Utilisation de `snprintf()` avec tailles de buffers correctes

**Description** : `snprintf()` est utilisé avec les bonnes tailles de buffers. Pas d'incohérence majeure.

**Impact** : Aucun.

---

## Statistiques par catégorie

| Catégorie | Critique | Important | Mineur | Total |
|-----------|----------|-----------|--------|-------|
| Version | 0 | 1 | 0 | 1 |
| Configuration | 1 | 2 | 1 | 4 |
| Pins/GPIO | 0 | 1 | 1 | 2 |
| Mémoire | 0 | 1 | 1 | 2 |
| Offline-first | 0 | 1 | 1 | 2 |
| Timeouts/Delays | 0 | 1 | 1 | 2 |
| Validation | 1 | 2 | 0 | 3 |
| Réseau | 0 | 1 | 1 | 2 |
| Nommage | 0 | 1 | 1 | 2 |
| Types | 0 | 1 | 1 | 2 |
| **TOTAL** | **3** | **12** | **8** | **23** |

---

## Priorisation des corrections

### Priorité 1 (Critique - À corriger immédiatement)

1. **Bug syntaxe nourrissage** (`src/automatism/automatism_feeding_schedule.cpp` ligne 42)
2. **Version non documentée** (`VERSION.md` manque versions 11.139-11.155)
3. **Constantes EPOCH dupliquées** (`include/config.h`)

### Priorité 2 (Important - À corriger rapidement)

4. **Validation température air** (`src/system_sensors.cpp` ligne 71)
5. **Validation heures nourrissage** (ajouter validation 0-23)
6. **Vérifications WebSocket** (ajouter `connectedClients() > 0`)
7. **Seuils serveur/ESP32** (synchroniser)
8. **Types NVS uint32_t** (utiliser saveULong/loadULong)
9. **Utilisation String** (remplacer par char[] progressivement)

### Priorité 3 (Mineur - À corriger à terme)

10. Scripts PowerShell avec versions hardcodées
11. Documentation GPIO mapping obsolète
12. Clés NVS avec préfixes incohérents
13. Timeout HTTP OTA à documenter
14. Stack sizes monitoring à améliorer

---

## Recommandations générales

1. **Processus de version** : Mettre à jour `VERSION.md` à chaque changement de version dans `config.h`
2. **Tests de validation** : Ajouter des tests unitaires pour les validations de plages (heures, températures, distances)
3. **Documentation** : Documenter les différences intentionnelles entre serveur et ESP32 (seuils, timeouts)
4. **Refactoring progressif** : Planifier le remplacement progressif des `String` par `char[]` dans les modules critiques
5. **Monitoring** : Ajouter des métriques pour détecter les incohérences à l'exécution (heap, stack, validations)

---

**Fin du rapport**
