# Rapport d'Analyse - Échanges avec le Serveur Distant

**Date:** 2026-01-25  
**Version du code analysé:** 11.156  
**Auteur:** Analyse automatique du codebase

---

## 📋 Table des Matières

1. [Vue d'ensemble](#vue-densemble)
2. [Architecture](#architecture)
3. [Composants Principaux](#composants-principaux)
4. [Flux de Données](#flux-de-données)
5. [Gestion des Erreurs et Retry](#gestion-des-erreurs-et-retry)
6. [Persistance des Données](#persistance-des-données)
7. [Sécurité et TLS](#sécurité-et-tls)
8. [Points d'Attention](#points-dattention)
9. [Recommandations](#recommandations)

---

## Vue d'Ensemble

Le système d'échanges avec le serveur distant (`iot.olution.info`) assure une communication bidirectionnelle fiable entre l'ESP32 et le serveur web. Il garantit la persistance des données en cas de panne réseau et gère intelligemment les retries et la récupération.

### Caractéristiques Principales

- **Envoi de données:** POST toutes les 2 minutes (120 secondes)
- **Réception de commandes:** GET toutes les 12 secondes (optimisé depuis 4 secondes)
- **Queue persistante:** Sauvegarde en LittleFS en cas d'échec réseau
- **Retry intelligent:** Backoff exponentiel avec maximum 3 tentatives
- **Sérialisation TLS:** Mutex pour éviter collisions SMTP/HTTPS
- **Tâche dédiée:** `netTask` propriétaire unique de toutes les opérations réseau

---

## Architecture

### Schéma Global

```
┌─────────────────────────────────────────────────────────────┐
│                    ESP32 - Système                          │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌──────────────┐      ┌──────────────┐                     │
│  │ automationTask│─────▶│ AutomatismSync│                    │
│  │  (Core 1)    │      │              │                     │
│  └──────────────┘      └──────┬───────┘                     │
│                                │                              │
│                                ▼                              │
│                        ┌──────────────┐                       │
│                        │  DataQueue   │                       │
│                        │  (LittleFS)  │                       │
│                        └──────────────┘                       │
│                                │                              │
│                                ▼                              │
│  ┌──────────────┐      ┌──────────────┐                     │
│  │   netTask    │◀─────│ AppTasks::   │                     │
│  │  (Core 0)    │      │ netPostRaw() │                     │
│  └──────┬───────┘      └──────────────┘                     │
│         │                                                      │
│         ▼                                                      │
│  ┌──────────────┐      ┌──────────────┐                     │
│  │  WebClient   │─────▶│  TLSMutex    │                     │
│  │              │      │  (Protection)│                     │
│  └──────┬───────┘      └──────────────┘                     │
│         │                                                      │
│         ▼                                                      │
│  ┌──────────────┐                                             │
│  │ WiFiClientSecure│                                           │
│  │  (HTTPS/TLS)  │                                             │
│  └──────┬───────┘                                             │
└─────────┼─────────────────────────────────────────────────────┘
          │
          ▼
┌─────────────────────────────────────────────────────────────┐
│         Serveur Distant: iot.olution.info                 │
├─────────────────────────────────────────────────────────────┤
│  • POST /ffp3/post-data (ou /post-data-test)               │
│  • GET  /ffp3/api/outputs/state (ou /outputs-test/state)  │
│  • POST /ffp3/heartbeat (ou /heartbeat-test)                │
└─────────────────────────────────────────────────────────────┘
```

### Tâches FreeRTOS Impliquées

| Tâche | Core | Priorité | Stack | Responsabilité |
|-------|------|----------|-------|----------------|
| `automationTask` | 1 | 3 | 8 KB | Orchestration, déclenchement envois |
| `netTask` | 0 | 2 | 12 KB | **Propriétaire unique** de WebClient/TLS |
| `sensorTask` | 1 | 2 | 4 KB | Lecture capteurs (données envoyées) |

---

## Composants Principaux

### 1. WebClient (`src/web_client.cpp`)

**Responsabilité:** Client HTTP/HTTPS pour toutes les communications avec le serveur distant.

#### Méthodes Publiques

- **`sendMeasurements(Measurements&, bool)`**: Envoie les données de capteurs
- **`postRaw(const char*)`**: Envoie un payload brut (utilisé par `AutomatismSync`)
- **`fetchRemoteState(JsonDocument&)`**: Récupère l'état distant (commandes, config)
- **`sendHeartbeat(Diagnostics&)`**: Envoie un heartbeat avec statistiques système

#### Caractéristiques Techniques

- **Client TLS:** `WiFiClientSecure` avec certificats acceptés (`setInsecure()`)
- **Timeout:** 5000ms (configuré via `NetworkConfig::HTTP_TIMEOUT_MS`)
- **Retry:** 3 tentatives maximum avec backoff exponentiel
- **Protection mémoire:** Vérification heap minimum (52 KB) avant requête HTTPS
- **Mutex TLS:** Utilise `TLSMutex` pour éviter collisions avec SMTP

#### Flux d'une Requête HTTP

```cpp
1. Vérification WiFi connecté
2. Vérification heap suffisant (52 KB minimum)
3. Acquisition mutex TLS (timeout 5s)
4. Désactivation modem sleep
5. Délai stabilisation (100ms)
6. Boucle retry (max 3 tentatives):
   - Initialisation HTTPClient
   - POST/GET avec timeout
   - Lecture réponse (buffer fixe 2048 bytes)
   - Vérification code HTTP (2xx-3xx = succès)
   - Backoff exponentiel si échec (sauf 4xx)
7. Libération mutex TLS
8. Reset client TLS (libère ~46 KB mémoire)
```

### 2. AutomatismSync (`src/automatism/automatism_sync.cpp`)

**Responsabilité:** Orchestration de la synchronisation bidirectionnelle.

#### Fonctionnalités

- **Envoi périodique:** Toutes les 2 minutes (`SEND_INTERVAL_MS = 120000`)
- **Réception périodique:** Toutes les 12 secondes (`REMOTE_FETCH_INTERVAL_MS = 12000`)
- **Gestion queue:** Push en cas d'échec, replay après succès
- **Backoff adaptatif:** Augmentation exponentielle après échecs consécutifs

#### Payload Envoyé

Le payload POST contient:
- **Capteurs:** TempAir, Humidite, TempEau, EauPotager, EauAquarium, EauReserve, diffMaree, Luminosite
- **Actionneurs:** etatPompeAqua, etatPompeTank, etatHeat, etatUV
- **Configuration:** bouffeMatin/Midi/Soir, tempsGros/Petits, seuils, FreqWakeUp, etc.
- **Métadonnées:** api_key, sensor, version, resetMode

#### Commandes Reçues

Le serveur peut envoyer des commandes via JSON:
- **`bouffePetits`** / **`bouffeGros`**: Déclenchement nourrissage manuel
- **Configuration:** Seuils, durées, horaires, email, etc.

### 3. DataQueue (`src/data_queue.cpp`)

**Responsabilité:** Queue persistante FIFO pour données non envoyées.

#### Caractéristiques

- **Stockage:** LittleFS (`/queue/pending_data.txt`)
- **Format:** JSON Lines (1 ligne = 1 payload)
- **Capacité:** 40 entrées maximum (configurable via `QUEUE_MAX_ENTRIES`)
- **Rotation:** Suppression automatique des plus anciennes si dépassement

#### Opérations

- **`push(payload)`**: Ajoute en fin de queue (append-only)
- **`pop(buffer)`**: Lit et supprime la première entrée
- **`peek(buffer)`**: Lit sans supprimer
- **`replayQueuedData()`**: Rejoue jusqu'à 5 entrées après succès

### 4. TLSMutex (`include/tls_mutex.h`)

**Responsabilité:** Protection contre collisions TLS (SMTP + HTTPS simultanés).

#### Problème Résolu

**Crash observé:** `strcat(NULL)` dans ESP Mail Client lors de connexions SMTP/HTTPS simultanées.

**Cause:** Mémoire insuffisante (~42 KB par connexion TLS × 2 = 84 KB requis).

**Solution:** Mutex global pour sérialiser toutes les connexions TLS.

#### Vérifications Avant Acquisition

1. **Light sleep:** Bloque si système entre en veille
2. **Heap minimum:** 52 KB requis (`TLS_MIN_HEAP_BYTES`)
3. **Double vérification:** Après acquisition, revérifie les conditions

### 5. netTask (`src/app_tasks.cpp`)

**Responsabilité:** Tâche dédiée propriétaire unique de `WebClient` et opérations TLS.

#### Architecture RPC

Les autres tâches envoient des requêtes via queue FreeRTOS:

```cpp
struct NetRequest {
    NetReqType type;        // FetchRemoteState, PostRaw, Heartbeat
    TaskHandle_t requester; // Tâche appelante (pour notification)
    uint32_t timeoutMs;
    bool success;
    
    // Données selon type
    JsonDocument* doc;      // Pour FetchRemoteState
    const Diagnostics* diag; // Pour Heartbeat
    char payload[1024];     // Pour PostRaw (copie)
};
```

#### Avantages

- **Isolation:** Une seule tâche manipule TLS (évite corruption mémoire)
- **Non-bloquant:** Les autres tâches ne sont pas bloquées par les opérations réseau
- **Watchdog:** `netTask` gère son propre watchdog
- **Boot:** Tente un `fetchRemoteState()` dès que WiFi disponible

---

## Flux de Données

### Envoi de Données (ESP32 → Serveur)

```
┌─────────────────────────────────────────────────────────────┐
│ 1. automationTask (toutes les 2 minutes)                    │
│    └─▶ AutomatismSync::update()                             │
│         └─▶ AutomatismSync::sendFullUpdate()               │
└─────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. Construction payload (1024 bytes max)                     │
│    • Capteurs + Actionneurs + Configuration                 │
│    • Format: application/x-www-form-urlencoded              │
└─────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. AppTasks::netPostRaw(payload, 30000)                    │
│    └─▶ Envoi via queue vers netTask                         │
└─────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│ 4. netTask reçoit requête                                   │
│    └─▶ WebClient::postRaw()                                │
│         └─▶ WebClient::httpRequest()                        │
│              ├─▶ Acquisition TLSMutex                       │
│              ├─▶ Retry loop (max 3)                        │
│              └─▶ POST https://iot.olution.info/ffp3/post-data
└─────────────────────────────────────────────────────────────┘
                          │
                    ┌─────┴─────┐
                    │           │
                  SUCCÈS      ÉCHEC
                    │           │
                    │           ▼
                    │    ┌──────────────────┐
                    │    │ DataQueue::push() │
                    │    │ (LittleFS)        │
                    │    └──────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────────────────────────┐
│ 5. Si succès ET queue non vide:                            │
│    └─▶ AutomatismSync::replayQueuedData()                  │
│         └─▶ Rejoue jusqu'à 5 entrées                         │
└─────────────────────────────────────────────────────────────┘
```

### Réception de Commandes (Serveur → ESP32)

```
┌─────────────────────────────────────────────────────────────┐
│ 1. automationTask (toutes les 12 secondes)                 │
│    └─▶ AutomatismSync::pollRemoteState()                   │
│         └─▶ AutomatismSync::fetchRemoteState()              │
└─────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. AppTasks::netFetchRemoteState(doc, 30000)               │
│    └─▶ Envoi via queue vers netTask                        │
└─────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. netTask reçoit requête                                   │
│    └─▶ WebClient::fetchRemoteState()                        │
│         └─▶ GET https://iot.olution.info/ffp3/api/outputs/state
│              ├─▶ Acquisition TLSMutex                       │
│              ├─▶ Lecture réponse (max 2048 bytes)          │
│              └─▶ Parse JSON                                 │
└─────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│ 4. Traitement JSON reçu                                     │
│    ├─▶ AutomatismSync::applyConfigFromJson()               │
│    │    └─▶ Mise à jour seuils, horaires, email, etc.      │
│    └─▶ AutomatismSync::handleRemoteFeedingCommands()       │
│         └─▶ Exécution nourrissage si commande reçue        │
└─────────────────────────────────────────────────────────────┘
```

---

## Gestion des Erreurs et Retry

### Stratégie de Retry (WebClient)

#### Paramètres

- **Tentatives maximum:** 3
- **Backoff:** Exponentiel (`BACKOFF_BASE_MS * attempt²`)
  - Tentative 1: 0ms (immédiat)
  - Tentative 2: 1000ms (1s)
  - Tentative 3: 4000ms (4s)
- **Timeout global:** 5000ms (`HTTP_TIMEOUT_MS`)
- **Timeout requête:** 5000ms (`REQUEST_TIMEOUT_MS`)

#### Conditions d'Arrêt

1. **Succès (2xx-3xx):** Arrêt immédiat
2. **Erreur client (4xx):** Pas de retry (erreur permanente)
3. **WiFi déconnecté:** Arrêt immédiat
4. **Timeout global:** Arrêt après 5s totales

#### Code de Retry

```cpp
// src/web_client.cpp:174-393
while (attempt < maxAttempts) {
    // ... tentative HTTP ...
    
    if (code >= 200 && code < 400) {
        break; // Succès
    }
    
    if (code >= 400 && code < 500) {
        LOG(LOG_WARN, "[HTTP] Erreur client %d, arrêt des retry", code);
        break; // Pas de retry sur 4xx
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        break; // WiFi perdu
    }
    
    // Backoff exponentiel
    int backoff = NetworkConfig::BACKOFF_BASE_MS * attempt * attempt;
    vTaskDelay(pdMS_TO_TICKS(backoff));
    attempt++;
}
```

### Backoff Adaptatif (AutomatismSync)

En plus du retry HTTP, `AutomatismSync` implémente un backoff adaptatif au niveau applicatif:

```cpp
// src/automatism/automatism_sync.cpp:350-359
void AutomatismSync::registerSendResult(bool success, ...) {
    if (success) {
        _consecutiveSendFailures = 0;
        _currentBackoffMs = 0;
    } else {
        if (_consecutiveSendFailures < 32) _consecutiveSendFailures++;
        _currentBackoffMs = 2000 << (_consecutiveSendFailures > 0 ? (_consecutiveSendFailures - 1) : 0);
        if (_currentBackoffMs > 60000) _currentBackoffMs = 60000; // Max 60s
    }
}
```

**Comportement:**
- **Échec 1:** Backoff 2s
- **Échec 2:** Backoff 4s
- **Échec 3:** Backoff 8s
- **Échec N:** Backoff min(2^N, 60s)

### Gestion de la Queue

#### Push en Cas d'Échec

```cpp
// src/automatism/automatism_sync.cpp:310-314
if (success) {
    _sendState = 1;
    _lastSend = millis();
    if (_dataQueue.size() > 0) replayQueuedData();
} else {
    _sendState = -1;
    _dataQueue.push(payloadBuffer); // Sauvegarde pour plus tard
}
```

#### Replay Après Succès

```cpp
// src/automatism/automatism_sync.cpp:361-377
uint16_t AutomatismSync::replayQueuedData() {
    uint16_t sent = 0;
    while (_dataQueue.size() > 0 && sent < 5) { // Max 5 par cycle
        char payload[1024];
        if (_dataQueue.peek(payload, sizeof(payload))) {
            if (AppTasks::netPostRaw(payload, 30000)) {
                _dataQueue.pop(payload, sizeof(payload));
                sent++;
            } else {
                break; // Échec, arrêter le replay
            }
        }
    }
    return sent;
}
```

**Limite:** Maximum 5 entrées rejouées par cycle pour éviter de bloquer le système.

---

## Persistance des Données

### DataQueue - Queue Persistante

#### Stockage

- **Fichier:** `/queue/pending_data.txt` (LittleFS)
- **Format:** JSON Lines (1 payload par ligne)
- **Capacité:** 40 entrées maximum

#### Opérations

**Push (ajout):**
```cpp
File file = LittleFS.open(QUEUE_FILE, FILE_APPEND);
file.println(payload); // Append-only pour performance
file.close();
```

**Pop (lecture + suppression):**
```cpp
// 1. Lire première ligne
// 2. Créer fichier temporaire
// 3. Copier toutes les lignes sauf la première
// 4. Remplacer fichier original
```

**Rotation:**
Si la queue dépasse 40 entrées, les plus anciennes sont supprimées automatiquement.

#### Avantages

- **Survie redémarrage:** Les données persistent même après reset
- **FIFO:** Garantit l'ordre d'envoi
- **Append-only:** Performance optimale pour ajout
- **Rotation automatique:** Évite saturation mémoire

### Sauvegarde Configuration Distante

Lors de la réception d'un état distant valide:

```cpp
// src/automatism/automatism_sync.cpp:319-333
bool AutomatismSync::fetchRemoteState(JsonDocument& doc) {
    bool ok = AppTasks::netFetchRemoteState(doc, 30000);
    if (ok && doc.size() > 0) {
        char jsonStr[2048];
        serializeJson(doc, jsonStr, sizeof(jsonStr));
        _config.saveRemoteVars(jsonStr); // Sauvegarde en NVS
        _serverOk = true;
        _recvState = 1;
    }
    return ok;
}
```

La configuration est sauvegardée en NVS pour persister après redémarrage.

---

## Sécurité et TLS

### Configuration TLS

#### Client HTTPS

```cpp
// src/web_client.cpp:40-55
WebClient::WebClient(const char* apiKey) {
    _client.setInsecure();               // Accepte tous certificats
    _client.setHandshakeTimeout(12000);  // 12s timeout handshake
    _http.setReuse(false);               // Pas de keep-alive
    _http.setTimeout(NetworkConfig::HTTP_TIMEOUT_MS);
}
```

**⚠️ Note:** `setInsecure()` accepte tous les certificats. En production, il serait préférable de valider les certificats.

#### Protection Mutex TLS

**Problème:** Collisions SMTP/HTTPS simultanées → crash (`strcat(NULL)`).

**Solution:** Mutex global `TLSMutex` pour sérialiser toutes les connexions TLS.

**Utilisation:**
```cpp
// src/web_client.cpp:890-894
if (!TLSMutex::acquire(5000)) {
    Serial.println(F("[PR] ⛔ Impossible d'acquérir mutex TLS - SKIP"));
    return false;
}
// ... opération TLS ...
TLSMutex::release();
```

#### Vérifications Avant TLS

1. **Heap minimum:** 52 KB requis (`TLS_MIN_HEAP_BYTES`)
2. **Light sleep:** Bloque si système entre en veille
3. **Double vérification:** Après acquisition mutex, revérifie les conditions

### Authentification

#### API Key

Toutes les requêtes incluent une API key:

```cpp
// include/config.h:220-222
namespace ApiConfig {
    constexpr const char* API_KEY = "fdGTMoptd5CD2ert3";
}
```

**⚠️ Note:** L'API key est hardcodée dans le code. En production, elle devrait être stockée en NVS ou générée dynamiquement.

#### Headers HTTP

```cpp
_http.addHeader("Content-Type", "application/x-www-form-urlencoded");
```

---

## Points d'Attention

### 1. Mémoire TLS

**Problème:** Chaque connexion TLS consomme ~42-46 KB de mémoire.

**Impact:** Si le heap est < 52 KB, les requêtes HTTPS sont refusées.

**Solution actuelle:**
- Vérification heap avant requête
- Reset client TLS après chaque requête (`resetTLSClient()`)
- Mutex pour éviter connexions simultanées

**Recommandation:** Monitorer le heap minimum en production.

### 2. Timeout Global vs Timeout Requête

**Configuration:**
- **Timeout global:** 5000ms (`HTTP_TIMEOUT_MS`)
- **Timeout requête:** 5000ms (`REQUEST_TIMEOUT_MS`)

**Problème potentiel:** Si une requête prend 4.9s et qu'il y a 3 tentatives, le timeout global peut être dépassé avant la fin des retries.

**Code actuel:**
```cpp
// src/web_client.cpp:198-205
uint32_t elapsedMs = millis() - requestStartMs;
if (elapsedMs >= NetworkConfig::HTTP_TIMEOUT_MS) {
    LOG(LOG_WARN, "[HTTP] Timeout global atteint: %u/%u ms", elapsedMs, NetworkConfig::HTTP_TIMEOUT_MS);
    _http.end();
    resetTLSClient();
    return false;
}
```

**✅ Bon:** Le timeout global est vérifié dans la boucle de retry.

### 3. Queue Limitée à 5 Replays

**Code:**
```cpp
while (_dataQueue.size() > 0 && sent < 5) { // Max 5 par cycle
```

**Impact:** Si la queue contient 40 entrées et que seulement 5 sont rejouées par cycle (toutes les 2 minutes), il faudra 16 cycles (32 minutes) pour vider la queue.

**Recommandation:** Augmenter la limite ou implémenter un replay plus agressif après une longue panne réseau.

### 4. Certificats TLS Non Validés

**Code:**
```cpp
_client.setInsecure(); // Accepte tous certificats
```

**Risque:** Vulnérable aux attaques man-in-the-middle.

**Recommandation:** Valider les certificats en production (nécessite stockage certificat racine).

### 5. API Key Hardcodée

**Code:**
```cpp
constexpr const char* API_KEY = "fdGTMoptd5CD2ert3";
```

**Risque:** Si le code est compromis, l'API key est exposée.

**Recommandation:** Stocker l'API key en NVS (chiffrée si possible).

### 6. Buffer de Réponse Limité

**Code:**
```cpp
const size_t MAX_TEMP_RESPONSE = 2048; // Taille max pour réponse temporaire
```

**Impact:** Si le serveur envoie une réponse > 2048 bytes, elle sera tronquée.

**Recommandation:** Vérifier que les réponses serveur restent < 2048 bytes, ou augmenter le buffer.

### 7. Délai Inter-Requêtes

**Code:**
```cpp
// src/web_client.cpp:143-152
if (_lastRequestMs > 0) {
    unsigned long timeSinceLastRequest = millis() - _lastRequestMs;
    if (timeSinceLastRequest < NetworkConfig::MIN_DELAY_BETWEEN_REQUESTS_MS) {
        uint32_t delayNeeded = NetworkConfig::MIN_DELAY_BETWEEN_REQUESTS_MS - timeSinceLastRequest;
        vTaskDelay(pdMS_TO_TICKS(delayNeeded));
    }
}
```

**Configuration:** `MIN_DELAY_BETWEEN_REQUESTS_MS = 1000` (1 seconde)

**✅ Bon:** Évite la saturation TCP/IP.

### 8. Modem Sleep Désactivé

**Code:**
```cpp
WiFi.setSleep(false); // Désactivé pendant transfert
```

**Impact:** Consommation énergétique accrue pendant les transferts.

**✅ Bon:** Nécessaire pour la fiabilité des transferts TLS.

---

## Recommandations

### Court Terme

1. **Augmenter limite replay queue:** Passer de 5 à 10-15 entrées par cycle
2. **Monitorer heap minimum:** Logger le heap minimum observé en production
3. **Valider taille réponses:** Vérifier que les réponses serveur restent < 2048 bytes

### Moyen Terme

1. **Validation certificats TLS:** Implémenter validation certificats (nécessite certificat racine)
2. **API key en NVS:** Déplacer l'API key vers NVS (avec migration depuis code)
3. **Buffer réponse dynamique:** Allouer dynamiquement selon `Content-Length` header

### Long Terme

1. **Chiffrement API key:** Chiffrer l'API key stockée en NVS
2. **Rotation API key:** Support pour rotation d'API key sans reflash
3. **Compression payload:** Implémenter compression gzip pour réduire taille payloads
4. **WebSocket bidirectionnel:** Remplacer polling GET par WebSocket pour latence réduite

---

## Conclusion

Le système d'échanges avec le serveur distant est **robuste et bien conçu**, avec:

✅ **Points forts:**
- Architecture claire avec tâche dédiée (`netTask`)
- Persistance des données (queue LittleFS)
- Retry intelligent avec backoff
- Protection contre collisions TLS (mutex)
- Gestion mémoire proactive (vérification heap)

⚠️ **Points d'amélioration:**
- Validation certificats TLS
- API key en NVS (non hardcodée)
- Limite replay queue (5 → 10-15)
- Buffer réponse (vérifier taille serveur)

Le système est **prêt pour la production** avec les améliorations de sécurité recommandées.

---

**Fin du rapport**
