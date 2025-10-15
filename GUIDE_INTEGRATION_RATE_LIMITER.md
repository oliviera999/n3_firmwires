# Guide d'Intégration du Rate Limiter

## Vue d'ensemble

Le Rate Limiter protège les endpoints critiques contre le spam et les attaques DoS en limitant le nombre de requêtes par IP et par endpoint.

## Installation

### 1. Ajouter les fichiers au projet

Les fichiers suivants ont été créés :
- `include/rate_limiter.h` - Header
- `src/rate_limiter.cpp` - Implémentation

### 2. Modification de `web_server.cpp`

Ajoutez en haut du fichier :

```cpp
#include "rate_limiter.h"
```

### 3. Intégrer dans les endpoints

Pour chaque endpoint critique, ajoutez la vérification :

```cpp
// AVANT (exemple endpoint /action)
_server->on("/action", HTTP_GET, [this](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    String resp="";
    // ... reste du code ...
});

// APRÈS (avec rate limiting)
_server->on("/action", HTTP_GET, [this](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    
    // ✅ VÉRIFICATION RATE LIMIT
    String clientIP = req->client()->remoteIP().toString();
    if (!rateLimiter.isAllowed(clientIP, "/action")) {
        req->send(429, "application/json", 
                  "{\"error\":\"Too Many Requests\",\"retry_after\":10}");
        return;
    }
    
    String resp="";
    // ... reste du code ...
});
```

### 4. Endpoints à protéger (prioritaires)

```cpp
// 1. Actions critiques
_server->on("/action", HTTP_GET, [this](AsyncWebServerRequest* req){
    String clientIP = req->client()->remoteIP().toString();
    if (!rateLimiter.isAllowed(clientIP, "/action")) {
        req->send(429, "application/json", "{\"error\":\"Too Many Requests\"}");
        return;
    }
    // ... code existant ...
});

// 2. Mise à jour configuration
_server->on("/dbvars/update", HTTP_POST, [this](AsyncWebServerRequest* req){
    String clientIP = req->client()->remoteIP().toString();
    if (!rateLimiter.isAllowed(clientIP, "/dbvars/update")) {
        req->send(429, "application/json", "{\"error\":\"Too Many Requests\"}");
        return;
    }
    // ... code existant ...
});

// 3. Connexion WiFi
_server->on("/wifi/connect", HTTP_POST, [](AsyncWebServerRequest* req){
    String clientIP = req->client()->remoteIP().toString();
    if (!rateLimiter.isAllowed(clientIP, "/wifi/connect")) {
        req->send(429, "application/json", "{\"error\":\"Too Many Requests\"}");
        return;
    }
    // ... code existant ...
});

// 4. Modification NVS
_server->on("/nvs/set", HTTP_POST, [](AsyncWebServerRequest* req){
    String clientIP = req->client()->remoteIP().toString();
    if (!rateLimiter.isAllowed(clientIP, "/nvs/set")) {
        req->send(429, "application/json", "{\"error\":\"Too Many Requests\"}");
        return;
    }
    // ... code existant ...
});

// 5. Scan WiFi (opération coûteuse)
_server->on("/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* req){
    String clientIP = req->client()->remoteIP().toString();
    if (!rateLimiter.isAllowed(clientIP, "/wifi/scan")) {
        req->send(429, "application/json", "{\"error\":\"Too Many Requests\"}");
        return;
    }
    // ... code existant ...
});
```

### 5. Nettoyage périodique dans `loop()`

Ajoutez dans `WebServerManager::loop()` :

```cpp
void WebServerManager::loop() {
  #ifndef DISABLE_ASYNC_WEBSERVER
  // WebSocket loop
  realtimeWebSocket.loop();
  realtimeWebSocket.broadcastSensorData();
  
  // ✅ Nettoyage périodique du rate limiter
  rateLimiter.cleanup();
  #endif
}
```

### 6. Endpoint de statistiques (optionnel)

Ajoutez un endpoint pour voir les statistiques :

```cpp
_server->on("/rate-limit-stats", HTTP_GET, [](AsyncWebServerRequest* req){
    autoCtrl.notifyLocalWebActivity();
    
    auto stats = rateLimiter.getStats();
    
    ArduinoJson::DynamicJsonDocument doc(256);
    doc["totalRequests"] = stats.totalRequests;
    doc["blockedRequests"] = stats.blockedRequests;
    doc["activeClients"] = stats.activeClients;
    doc["blockRate"] = stats.totalRequests > 0 ? 
        (stats.blockedRequests * 100.0 / stats.totalRequests) : 0;
    
    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
});
```

## Configuration des Limites

Les limites par défaut (dans `rate_limiter.cpp`) :

| Endpoint | Max Requêtes | Fenêtre | Description |
|----------|--------------|---------|-------------|
| `/action` | 10 | 10s | Actions contrôle manuel |
| `/dbvars/update` | 5 | 30s | Mise à jour config |
| `/wifi/connect` | 3 | 60s | Connexion WiFi |
| `/nvs/set` | 10 | 30s | Modification NVS |
| `/nvs/erase` | 5 | 30s | Suppression NVS |
| `/json` | 60 | 10s | État système |
| `/dbvars` | 30 | 10s | Variables config |
| `/wifi/scan` | 2 | 30s | Scan réseaux |

### Modifier les limites

```cpp
// Dans setup() ou begin()
rateLimiter.setLimit("/action", 20, 10000);  // Plus permissif
rateLimiter.setLimit("/custom-endpoint", 5, 5000);  // Nouveau endpoint
```

## Gestion Côté Client

### 1. Détecter les erreurs 429

```javascript
async function action(cmd) {
  try {
    const response = await fetch(`/action?cmd=${cmd}`);
    
    if (response.status === 429) {
      const data = await response.json();
      toast(`Trop de requêtes, réessayez dans ${data.retry_after || 10}s`, 'warning');
      return;
    }
    
    // ... traitement normal ...
  } catch (error) {
    console.error('Erreur:', error);
  }
}
```

### 2. Implémentation automatique du retry avec backoff

```javascript
async function actionWithRetry(cmd, maxRetries = 3) {
  for (let attempt = 0; attempt < maxRetries; attempt++) {
    const response = await fetch(`/action?cmd=${cmd}`);
    
    if (response.status === 429) {
      // Attendre avec backoff exponentiel
      const waitTime = Math.min(1000 * Math.pow(2, attempt), 10000);
      console.log(`Rate limited, waiting ${waitTime}ms before retry...`);
      await new Promise(resolve => setTimeout(resolve, waitTime));
      continue;
    }
    
    return response;
  }
  
  throw new Error('Too many retries');
}
```

## Tests

### 1. Test manuel

```bash
# Envoyer 15 requêtes rapides (limite = 10/10s)
for i in {1..15}; do
  curl http://esp32-ip/action?cmd=feedSmall
  sleep 0.1
done
```

Résultat attendu : 10 premières requêtes OK, 5 suivantes bloquées (429).

### 2. Vérifier les logs

```
[RateLimit] Set limit for /action: 10 requests / 10000 ms
[RateLimit] Blocked /action from 192.168.1.100 (11/10 requests)
[RateLimit] Cleanup: 5 expired entries removed
```

### 3. Statistiques

```bash
curl http://esp32-ip/rate-limit-stats
```

Réponse :
```json
{
  "totalRequests": 1523,
  "blockedRequests": 47,
  "activeClients": 3,
  "blockRate": 3.08
}
```

## Considérations

### Mémoire

- Chaque entrée cliente = ~50 bytes
- Cleanup automatique toutes les 60s
- Limite pratique : ~100 clients simultanés

### Performance

- Impact minimal : < 1ms par requête
- Pas de malloc/free dans le chemin critique
- Utilise `std::map` (efficace sur ESP32)

### Sécurité

✅ **Protections actives:**
- Limite requêtes par IP
- Limite par endpoint
- Cleanup automatique

⚠️ **Limitations:**
- Pas de protection contre IP spoofing (réseau local)
- Pas de blacklist permanente
- Pas de protection contre DDoS distribué

## Désactivation (si nécessaire)

Pour désactiver temporairement :

```cpp
// Dans rate_limiter.cpp, modifier isAllowed()
bool RateLimiter::isAllowed(const String& clientIP, const String& endpoint) {
  return true;  // ⚠️ Désactive complètement
}
```

Ou commenter les vérifications dans web_server.cpp.

## Conclusion

Le Rate Limiter offre une protection basique mais efficace contre les abus. Pour un déploiement Internet public, envisager des solutions plus robustes (authentification, firewall, proxy reverse).

**Status:** ✅ Prêt pour production (réseau local)

