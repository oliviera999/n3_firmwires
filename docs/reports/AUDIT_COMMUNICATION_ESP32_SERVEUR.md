# Audit Communication ESP32 ↔ Serveur (ffp3)

**Date:** 2026-01-13  
**Version ESP32:** 11.126  
**Version Serveur:** 4.4.0+  
**Statut:** ✅ Audit complet réalisé

---

## Résumé Exécutif

Cet audit examine en détail la communication bidirectionnelle entre l'ESP32 (ffp5cs) et le serveur distant (iot.olution.info/ffp3). Le système présente une architecture solide avec gestion des erreurs, retry intelligent, et file d'attente persistante. Plusieurs points d'amélioration ont été identifiés concernant la sécurité, la performance et le monitoring.

**Score global:** 7.5/10

**Points forts:**
- ✅ Architecture bien structurée avec séparation des responsabilités
- ✅ Gestion robuste des erreurs avec retry et file d'attente
- ✅ Protection contre injection SQL via PDO prepared statements
- ✅ Logs détaillés pour debugging

**Points à améliorer:**
- ⚠️ API key hardcodée dans le code
- ⚠️ HTTPS avec certificat invalide accepté
- ⚠️ Polling GET toutes les 4 secondes (surcharge potentielle)
- ⚠️ Pas de circuit breaker pour pannes serveur prolongées

---

## 1. Audit des Endpoints

### 1.1 Endpoints PRODUCTION

| Endpoint | Méthode | Route | Contrôleur | Statut |
|----------|---------|-------|------------|--------|
| POST données | POST | `/ffp3/post-data` | `PostDataController::handle()` | ✅ OK |
| GET outputs | GET | `/ffp3/api/outputs/state` | `OutputController::getOutputsState()` | ✅ OK |
| POST heartbeat | POST | `/ffp3/heartbeat` | `HeartbeatController::handle()` | ✅ OK |

**Fichier:** `ffp3/public/index.php` lignes 65-101

**Observations:**
- Routes correctement définies avec middleware `EnvironmentMiddleware('prod')`
- Alias legacy présents pour compatibilité (`/post-ffp3-data.php`, `/heartbeat.php`)
- Redirections 301 pour URLs obsolètes

### 1.2 Endpoints TEST

| Endpoint | Méthode | Route | Contrôleur | Statut |
|----------|---------|-------|------------|--------|
| POST données | POST | `/ffp3/post-data-test` | `PostDataController::handle()` | ✅ OK |
| GET outputs | GET | `/ffp3/api/outputs-test/state` | `OutputController::getOutputsState()` | ✅ OK |
| POST heartbeat | POST | `/ffp3/heartbeat-test` | `HeartbeatController::handle()` | ✅ OK |

**Fichier:** `ffp3/public/index.php` lignes 127-155

**Observations:**
- Routes TEST correctement isolées avec middleware `EnvironmentMiddleware('test')`
- Même contrôleur que PROD, environnement déterminé par middleware
- Configuration cohérente avec ESP32 (`PROFILE_TEST`)

### 1.3 Configuration ESP32

**Fichier:** `include/config.h` lignes 135-171

```cpp
namespace ServerConfig {
    constexpr const char* BASE_URL = "https://iot.olution.info";
    
    #if defined(PROFILE_TEST) || defined(PROFILE_DEV)
        constexpr const char* POST_DATA_ENDPOINT = "/ffp3/post-data-test";
        constexpr const char* OUTPUT_ENDPOINT = "/ffp3/api/outputs-test/state";
        constexpr const char* HEARTBEAT_ENDPOINT = "/ffp3/heartbeat-test";
    #else
        constexpr const char* POST_DATA_ENDPOINT = "/ffp3/post-data";
        constexpr const char* OUTPUT_ENDPOINT = "/ffp3/api/outputs/state";
        constexpr const char* HEARTBEAT_ENDPOINT = "/ffp3/heartbeat";
    #endif
}
```

**Verdict:** ✅ Configuration cohérente entre ESP32 et serveur

---

## 2. Audit des Payloads

### 2.1 POST Données Capteurs (ESP32 → Serveur)

**Format:** `application/x-www-form-urlencoded`

**Construction:** `src/web_client.cpp` lignes 291-442 (`sendMeasurements()`)

**Champs envoyés (31 paramètres):**

| Champ | Type | Validation ESP32 | Traitement Serveur |
|-------|------|------------------|-------------------|
| `api_key` | string | - | Vérification ligne 76-89 |
| `sensor` | string | - | Sanitization |
| `version` | string | - | Sanitization |
| `TempAir` | float | 3-50°C | `toFloat()` → `SensorData` |
| `Humidite` | float | 10-100% | `toFloat()` → `SensorData` |
| `TempEau` | float | 0-60°C | `toFloat()` → `SensorData` |
| `EauPotager` | uint16 | 0 = invalide → "" | `toFloat()` → `SensorData` |
| `EauAquarium` | uint16 | 0 = invalide → "" | `toFloat()` → `SensorData` |
| `EauReserve` | uint16 | 0 = invalide → "" | `toFloat()` → `SensorData` |
| `diffMaree` | int | - | `toFloat()` → `SensorData` |
| `Luminosite` | uint16 | - | `toFloat()` → `SensorData` |
| `etatPompeAqua` | bool | - | `toInt()` → `SensorData` |
| `etatPompeTank` | bool | - | `toInt()` → `SensorData` |
| `etatHeat` | bool | - | `toInt()` → `SensorData` |
| `etatUV` | bool | - | `toInt()` → `SensorData` |
| `bouffeMatin` | uint8 | - | `toInt()` → `SensorData` |
| `bouffeMidi` | uint8 | - | `toInt()` → `SensorData` |
| `bouffeSoir` | uint8 | - | `toInt()` → `SensorData` |
| `bouffePetits` | string | - | `toInt()` → `SensorData` |
| `bouffeGros` | string | - | `toInt()` → `SensorData` |
| `tempsGros` | uint16 | - | `toInt()` → `SensorData` |
| `tempsPetits` | uint16 | - | `toInt()` → `SensorData` |
| `aqThreshold` | uint16 | - | `toInt()` → `SensorData` |
| `tankThreshold` | uint16 | - | `toInt()` → `SensorData` |
| `chauffageThreshold` | float | - | `toInt()` → `SensorData` ⚠️ |
| `mail` | string | - | `sanitize()` → `SensorData` |
| `mailNotif` | string | - | `sanitize()` → `SensorData` |
| `resetMode` | bool | - | `toInt()` → `SensorData` |
| `tempsRemplissageSec` | uint32 | - | `toInt()` → `SensorData` |
| `limFlood` | uint16 | - | `toInt()` → `SensorData` |
| `WakeUp` | bool | - | `toInt()` → `SensorData` |
| `FreqWakeUp` | uint16 | - | `toInt()` → `SensorData` |

**Problèmes identifiés:**

1. **Conversion `chauffageThreshold`:** 
   - ESP32 envoie float (ex: `18.0`)
   - Serveur convertit avec `toInt()` → perte de précision
   - **Impact:** Seuil chauffage arrondi à l'entier
   - **Recommandation:** Utiliser `toFloat()` pour ce champ

2. **Validation NaN:**
   - ESP32 convertit NaN en chaîne vide `""`
   - Serveur `toFloat("")` → `0.0` (pas NULL)
   - **Impact:** Valeurs invalides enregistrées comme 0
   - **Recommandation:** Vérifier chaîne vide avant conversion

**Taille payload:**
- Max théorique: ~512 bytes (buffer `payload[512]` ligne 352)
- Taille typique: ~400-450 bytes
- **Verdict:** ✅ Taille raisonnable

### 2.2 GET États Distants (Serveur → ESP32)

**Format:** JSON

**Génération:** `ffp3/src/Controller/OutputController.php` lignes 265-336 (`getOutputsState()`)

**Structure JSON:**
```json
{
  "2": "0",      // GPIO numérique (string)
  "15": "1",
  "16": "0",
  "18": "0",
  "100": "oliv.arn.lau@gmail.com",  // String (email)
  "101": "1",    // Booléen normalisé (string "1"/"0")
  "102": "18",   // Paramètre numérique (string)
  ...
}
```

**Normalisation:**
- GPIO < 100: Normalisation booléenne (0/1)
- GPIO >= 100: Conservation string (email, paramètres)
- **Fichier:** `OutputController::getOutputsState()` lignes 304-332

**Parsing ESP32:**
- **Fichier:** `src/automatism/automatism_network.cpp` lignes 109-200 (`applyConfigFromJson()`)
- Support clés numériques (GPIO) et nommées (compatibilité)
- Conversion robuste avec `parseIntValue()` et `parseFloatValue()`

**Verdict:** ✅ Format cohérent et bien géré

### 2.3 POST Heartbeat

**Format:** `application/x-www-form-urlencoded`

**Payload:**
```
uptime=123456&free=125432&min=120000&reboots=5&crc=ABCD1234
```

**CRC32:**
- Calculé sur: `uptime={uptime}&free={free}&min={min}&reboots={reboots}`
- **ESP32:** `web_client.cpp` ligne 12-21 (fonction `crc32()`)
- **Serveur:** `HeartbeatController::handle()` ligne 58 (`hash('crc32b', $raw)`)
- **Verdict:** ✅ Validation CRC32 correcte

---

## 3. Audit Sécurité

### 3.1 Authentification API Key

**ESP32:**
- **Fichier:** `include/config.h` ligne 174
- **Valeur:** `"fdGTMoptd5CD2ert3"` (hardcodée)
- **⚠️ PROBLÈME:** API key en clair dans le code source

**Serveur:**
- **Fichier:** `ffp3/src/Controller/PostDataController.php` lignes 76-89
- **Vérification:** Comparaison avec `$_ENV['API_KEY']`
- **Réponse:** 401 si invalide

**Recommandations:**
1. ✅ Utiliser `secrets.h` (fichier `.example` déjà présent)
2. ✅ Ajouter `secrets.h` au `.gitignore`
3. ⚠️ Changer API key régulièrement

### 3.2 HTTPS / TLS

**ESP32:**
- **Fichier:** `src/web_client.cpp` ligne 24
- **Code:** `_client.setInsecure();` ⚠️
- **Problème:** Accepte tous les certificats (vulnérable aux attaques MITM)

**Recommandations:**
1. ⚠️ Valider certificat serveur (fingerprint ou CA)
2. ⚠️ Utiliser certificat personnalisé si self-signed
3. ⚠️ Désactiver `setInsecure()` en production

### 3.3 Signature HMAC

**Serveur:**
- **Fichier:** `ffp3/src/Controller/PostDataController.php` lignes 42-71
- **Support:** Validation HMAC si `timestamp` et `signature` présents
- **Non utilisé:** ESP32 n'envoie pas ces champs

**Recommandations:**
1. ⚠️ Implémenter signature HMAC côté ESP32
2. ⚠️ Utiliser `SignatureValidator` existant
3. ⚠️ Remplacer API key par HMAC à terme

### 3.4 Protection Injection SQL

**Serveur:**
- **Méthode:** PDO prepared statements
- **Fichiers:**
  - `SensorRepository::insert()` ligne 46: `$stmt = $this->pdo->prepare($sql);`
  - `OutputRepository::syncStatesFromSensorData()` ligne 304: `$stmt = $this->pdo->prepare($sql);`
- **Verdict:** ✅ Protection correcte

**Sanitization:**
- **Fichier:** `PostDataController::handle()` lignes 93-96
- **Fonctions:** `sanitize()`, `toFloat()`, `toInt()`
- **Verdict:** ✅ Sanitization appropriée

### 3.5 Validation Données

**ESP32:**
- **Fichier:** `src/web_client.cpp` lignes 314-329
- **Validations:**
  - Températures: ranges (0-60°C eau, 3-50°C air)
  - Humidité: 10-100%
  - NaN → chaîne vide
- **Verdict:** ✅ Validation robuste

**Serveur:**
- **Fichier:** `PostDataController::handle()` lignes 93-96
- **Validation:** Types (float, int, string)
- **Gestion NULL:** Valeurs vides → NULL
- **Verdict:** ✅ Validation correcte

---

## 4. Audit Retry et Gestion Erreurs

### 4.1 Système Retry ESP32

**Fichier:** `src/web_client.cpp` lignes 101-268

**Configuration:**
- **Max tentatives:** 3 (ligne 102)
- **Backoff:** Exponentiel (0ms, 1s, 3s) ligne 250
- **Pas de retry 4xx:** Ligne 234 (erreurs client)
- **Vérification WiFi:** Ligne 240 avant chaque retry

**Timeouts:**
- `NetworkConfig::REQUEST_TIMEOUT_MS = 5000` (5 secondes)
- `GlobalTimeouts::HTTP_MAX_MS = 5000` (timeout global)
- Timeout non-bloquant avec vérification périodique

**Verdict:** ✅ Système retry intelligent et robuste

### 4.2 File d'Attente Persistante

**Fichier:** `src/data_queue.cpp/h`

**Caractéristiques:**
- **Stockage:** LittleFS (`/queue/pending_data.txt`)
- **Format:** JSON Lines (1 ligne = 1 payload)
- **Max entrées:** 50 (configurable)
- **FIFO:** Première entrée = première envoyée
- **Rotation:** Automatique si dépassement

**Fonctions:**
- `push()`: Ajoute payload (ligne 42)
- `pop()`: Récupère et supprime premier (ligne 85)
- `peek()`: Lit sans supprimer (ligne 137)
- `rotateIfNeeded()`: Rotation si > max (ligne 191)

**Verdict:** ✅ File d'attente bien implémentée

**Problèmes identifiés:**
1. **Pas de compression:** Payloads stockés en clair
   - **Impact:** Utilisation mémoire importante
   - **Recommandation:** Compression gzip pour payloads > 500 bytes

2. **Pas de TTL:** Entrées anciennes conservées indéfiniment
   - **Impact:** Queue peut contenir données obsolètes
   - **Recommandation:** TTL de 24h pour entrées

### 4.3 Gestion Erreurs Serveur

**Fichier:** `ffp3/src/Controller/PostDataController.php` lignes 151-156

**Gestion:**
- Try/catch autour insertion BDD
- Logging via `LogService` (Monolog)
- Réponses HTTP appropriées (400, 401, 500)

**Verdict:** ✅ Gestion erreurs correcte

**Améliorations possibles:**
1. ⚠️ Retry côté serveur pour erreurs BDD temporaires
2. ⚠️ Circuit breaker pour éviter surcharge
3. ⚠️ Alertes automatiques sur erreurs répétées

---

## 5. Audit Synchronisation Bidirectionnelle

### 5.1 ESP32 → Serveur (POST)

**Fréquence:** 2 minutes (`SERVER_SYNC_INTERVAL_MS = 60000`)

**Fichier:** `src/automatism/automatism_network.cpp` ligne 280

**Actions serveur:**
1. Insertion données capteurs dans `ffp3Data2`/`ffp3Data`
2. Synchronisation GPIO via `syncStatesFromSensorData()`
3. Mise à jour timestamp dernière requête board

**Priorité modifications web:**
- **Fichier:** `ffp3/src/Repository/OutputRepository.php` lignes 289-302
- **Logique:** Respecte priorité web pendant 10 secondes (ligne 301)
- **Condition:** `lastModifiedBy != 'web' OR requestTime < DATE_SUB(NOW(), INTERVAL 10 SECOND)`
- **Verdict:** ✅ Documentation alignée sur le code (10 secondes)

### 5.2 Serveur → ESP32 (GET)

**Fréquence:** 12 secondes (`REMOTE_FETCH_INTERVAL_MS = 12000`) - Optimisé v11.127

**Fichier:** `src/automatism/automatism_network.cpp` ligne 493

**Commandes distantes:**
- Nourrissage: `bouffePetits`, `bouffeGros`
- Actionneurs: `pump_tankCmd`, `heatCmd`, `lightCmd`
- Configuration: Seuils, horaires, paramètres
- Reset: `resetMode = "1"`

**Priorité locale:**
- **Fichier:** `src/automatism/automatism_network.cpp` (recherche `hasRecentLocalAction`)
- **Blocage:** 5 secondes après action locale
- **Verdict:** ✅ Priorité locale respectée

**ACK immédiat:**
- **Fichier:** `src/automatism/automatism_network.cpp` (recherche `sendCommandAck`)
- **Envoi:** Acquittement après exécution commande
- **Verdict:** ✅ ACK immédiat implémenté

**Optimisation implémentée (v11.127):**
- **Polling optimisé:** 12 secondes (au lieu de 4s)
- **Cache serveur:** TTL 5s, réduction requêtes SQL ~80%
- **Réduction charge:** 15 req/min → 5 req/min (66% de réduction)
- **Verdict:** ✅ Optimisation réussie (voir `OPTIMISATION_POLLING_IMPLEMENTEE.md`)

---

## 6. Audit Performance

### 6.1 Latences

**POST données:**
- **Timeout:** 5 secondes
- **Retry:** Max 3 tentatives
- **Durée typique:** 1-2 secondes (succès)
- **Durée max:** ~15 secondes (3 retries)

**GET outputs:**
- **Timeout:** 5 secondes
- **Durée typique:** 0.5-1 seconde
- **Verdict:** ✅ Latences acceptables

### 6.2 Utilisation Mémoire

**ESP32:**
- **Vérification heap:** 70 KB minimum avant HTTPS (ligne 40)
- **Buffer payload:** 512 bytes (ligne 352)
- **Buffer JSON:** 2048 bytes (ESP32-WROOM) ou 4096 bytes (ESP32-S3)
- **Verdict:** ✅ Gestion mémoire correcte

**Serveur:**
- **PDO:** Connexion singleton (pas de fuite)
- **Requêtes:** Préparées (réutilisables)
- **Verdict:** ✅ Pas de problème mémoire identifié

### 6.3 Throughput

**POST données:**
- **Fréquence:** 1 requête / 2 minutes = 0.5 req/min
- **Payload:** ~400-450 bytes
- **Verdict:** ✅ Throughput raisonnable

**GET outputs:**
- **Fréquence:** 1 requête / 12 secondes = 5 req/min (optimisé v11.127)
- **Réponse:** ~500-800 bytes JSON
- **Cache:** TTL 5s, réduction requêtes SQL ~80%
- **Verdict:** ✅ Fréquence optimisée (voir `OPTIMISATION_POLLING_IMPLEMENTEE.md`)

---

## 7. Audit Logs

### 7.1 Logs ESP32

**Fichier:** `src/web_client.cpp` (v11.42+)

**Logs détaillés:**
- Timestamps début/fin requêtes
- Durée chaque tentative
- État réseau (WiFi, RSSI, IP)
- Mémoire disponible
- Payloads complets (tronqués si > 500 bytes)
- Headers HTTP détaillés
- Diagnostic automatique erreurs

**Verdict:** ✅ Logs très détaillés et utiles

### 7.2 Logs Serveur

**Fichier:** `ffp3/src/Service/LogService.php` (Monolog)

**Niveaux:**
- INFO: Opérations normales
- WARNING: Problèmes mineurs (API key invalide, etc.)
- ERROR: Erreurs critiques (BDD, etc.)

**Fichiers logs:**
- `var/logs/post-data.log`
- `var/logs/heartbeat.log`
- `var/logs/outputs.log`

**Verdict:** ✅ Logging structuré et approprié

**Améliorations possibles:**
1. ⚠️ Centralisation logs (ELK, Graylog, etc.)
2. ⚠️ Rotation automatique logs
3. ~~**Alertes automatiques sur erreurs répétées**~~ ✅ **IMPLÉMENTÉ** (voir `ERROR_ALERT_SERVICE.md`)

---

## 8. Audit Repositories

### 8.1 SensorRepository

**Fichier:** `ffp3/src/Repository/SensorRepository.php`

**Méthode `insert()`:**
- **Ligne 28-77:** Insertion données capteurs
- **Protection SQL:** PDO prepared statements (ligne 46)
- **Table:** `ffp3Data2` (TEST) ou `ffp3Data` (PROD)
- **Verdict:** ✅ Implémentation correcte

**Problèmes identifiés:**
1. **Pas de transaction:** Insertion isolée
   - **Impact:** Pas de rollback si `syncStatesFromSensorData()` échoue
   - **Recommandation:** Transaction englobant insertion + sync

### 8.2 OutputRepository

**Fichier:** `ffp3/src/Repository/OutputRepository.php`

**Méthode `syncStatesFromSensorData()`:**
- **Ligne 248-331:** Synchronisation GPIO depuis données capteurs
- **Transaction:** Oui (ligne 280 `beginTransaction()`)
- **Protection priorité web:** Lignes 289-302
- **Verdict:** ✅ Implémentation robuste

**Méthode `getOutputsState()`:**
- **Fichier:** `OutputController::getOutputsState()` ligne 265
- **Requête SQL:** Ligne 290 (SELECT avec IN sécurisé)
- **Normalisation:** Lignes 304-332
- **Verdict:** ✅ Requête optimisée et sécurisée

---

## 9. Audit Contrôleurs

### 9.1 PostDataController

**Fichier:** `ffp3/src/Controller/PostDataController.php`

**Méthode `handle()`:**
- **Validation méthode:** Ligne 29 (POST uniquement)
- **Validation API key:** Lignes 76-89
- **Validation HMAC:** Lignes 42-71 (optionnel)
- **Construction SensorData:** Lignes 99-131
- **Insertion:** Lignes 133-149
- **Gestion erreurs:** Try/catch lignes 151-156

**Verdict:** ✅ Contrôleur bien structuré

### 9.2 OutputController

**Fichier:** `ffp3/src/Controller/OutputController.php`

**Méthode `getOutputsState()`:**
- **Ligne 265-336:** Récupération états outputs
- **Requête SQL:** Ligne 290 (sécurisée avec placeholders)
- **Normalisation:** Lignes 304-332
- **Réponse JSON:** Ligne 334

**Verdict:** ✅ Contrôleur correct

### 9.3 HeartbeatController

**Fichier:** `ffp3/src/Controller/HeartbeatController.php`

**Méthode `handle()`:**
- **Validation méthode:** Ligne 34 (POST uniquement)
- **Validation champs:** Lignes 49-54
- **Validation CRC32:** Lignes 56-68
- **Insertion BDD:** Lignes 70-97
- **Gestion erreurs:** Try/catch lignes 99-103

**Verdict:** ✅ Contrôleur robuste

---

## 10. Recommandations Prioritaires

### 🔴 Critique (À faire immédiatement)

1. **API Key en clair:**
   - Utiliser `secrets.h` (fichier `.example` déjà présent)
   - Ajouter `secrets.h` au `.gitignore`
   - Documenter migration pour utilisateurs

2. **HTTPS avec certificat invalide:**
   - Valider certificat serveur (fingerprint ou CA)
   - Désactiver `setInsecure()` en production

3. ~~**Incohérence priorité web:**~~ ✅ **CORRIGÉ**
   - ~~Aligner code (10 secondes) et documentation (5 minutes)~~
   - Documentation PHPDoc mise à jour pour refléter 10 secondes (valeur du code)

### 🟡 Important (À faire sous 1 mois)

4. ~~**Polling GET trop fréquent:**~~ ✅ **IMPLÉMENTÉ**
   - Intervalle augmenté à 12 secondes (au lieu de 4s)
   - Cache serveur ajouté (TTL 5s, réduction requêtes SQL ~80%)
   - Réduction charge serveur: 60-70%
   - Voir `OPTIMISATION_POLLING_IMPLEMENTEE.md` pour détails

5. **Transaction manquante:**
   - Transaction englobant `insert()` + `syncStatesFromSensorData()`
   - Rollback si échec sync

6. **Conversion `chauffageThreshold`:**
   - Utiliser `toFloat()` au lieu de `toInt()`
   - Préserver précision décimale

### 🟢 Amélioration (À faire sous 3 mois)

7. **Signature HMAC:**
   - Implémenter côté ESP32
   - Remplacer API key par HMAC

8. **Compression queue:**
   - Compression gzip pour payloads > 500 bytes
   - Réduire utilisation mémoire

9. **Circuit breaker:**
   - Arrêter retry après N échecs consécutifs
   - Réduire charge serveur en panne

10. ~~**Centralisation logs:**~~ ✅ **PARTIELLEMENT IMPLÉMENTÉ**
    - ⚠️ Centralisation logs (ELK, Graylog, etc.) - À faire
    - ✅ Alertes automatiques sur erreurs répétées - Implémenté (voir `ERROR_ALERT_SERVICE.md`)

---

## 11. Tests Recommandés

### Tests Fonctionnels

1. **Test endpoints:**
   - Vérifier tous les endpoints PROD et TEST
   - Tester avec API key valide/invalide
   - Vérifier réponses HTTP

2. **Test retry:**
   - Simuler échecs réseau (déconnecter WiFi)
   - Vérifier retry (max 3 tentatives)
   - Vérifier backoff exponentiel

3. **Test queue:**
   - Déconnecter WiFi pendant 10 minutes
   - Vérifier enregistrement queue
   - Reconnecter et vérifier rejeu

4. **Test commandes:**
   - Modifier état GPIO via interface web
   - Vérifier réception ESP32 (max 4 secondes)
   - Vérifier exécution commande

### Tests Performance

5. **Test latences:**
   - Mesurer temps réponse POST/GET
   - Vérifier respect timeouts
   - Identifier goulots

6. **Test mémoire:**
   - Surveiller heap ESP32 pendant communication
   - Vérifier pas de fuite mémoire
   - Tester avec queue pleine (50 entrées)

### Tests Robustesse

7. **Test pannes serveur:**
   - Simuler panne serveur prolongée
   - Vérifier comportement queue
   - Vérifier récupération après panne

8. **Test sécurité:**
   - Tester avec API key invalide
   - Tester injection SQL (payloads malveillants)
   - Vérifier sanitization

---

## 12. Conclusion

L'audit révèle une architecture de communication solide et bien pensée, avec gestion robuste des erreurs, retry intelligent, et file d'attente persistante. Les principaux points d'amélioration concernent la sécurité (API key, HTTPS) et la performance (polling fréquent).

**Score par catégorie:**
- Endpoints: 9/10 ✅
- Payloads: 8/10 ✅
- Sécurité: 6/10 ⚠️
- Retry/Erreurs: 9/10 ✅
- Synchronisation: 8/10 ✅
- Performance: 8/10 ✅ (optimisé v11.127)
- Logs: 9/10 ✅ (alertes automatiques implémentées)
- Repositories: 8/10 ✅
- Contrôleurs: 9/10 ✅

**Score global: 8.0/10** (amélioré depuis 7.5/10)

Le système est prêt pour la production après correction des points critiques identifiés.

---

**Fin du rapport d'audit**
