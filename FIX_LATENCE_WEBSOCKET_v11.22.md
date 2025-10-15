# Fix Latence Excessive et Erreur HTTP 500 - Version 11.22
**Date**: 2025-10-13  
**Version**: 11.21 → 11.22

## 🎯 Problèmes identifiés

### 1. Erreur HTTP 500 sur HEAD /json
```
HEAD http://192.168.0.86/json net::ERR_ABORTED 500 (Internal Server Error)
```
- **Cause**: Le serveur ESP32 ne gérait que GET et OPTIONS, pas HEAD
- **Impact**: Test de connectivité côté client échouait

### 2. Latence excessive (5612ms vs cible <100ms)
```
[DIAGNOSTIC] ⚠️ Latence serveur élevée: 5612ms
```
- **Cause**: Appel HTTP **bloquant** vers serveur distant dans `/dbvars`
- **Impact**: 
  - Timeouts sur `/json` et `/dbvars`
  - WebSocket instable
  - Interface web lente ou non réactive

### 3. Timeouts clients trop élevés
- 6 secondes de timeout pour des requêtes qui devraient prendre <100ms
- Dégradation de l'expérience utilisateur

## ✅ Corrections apportées

### 1. Support HTTP HEAD sur /json
**Fichier**: `src/web_server.cpp`

```cpp
// Avant: Seulement GET et OPTIONS
_server->on("/json", HTTP_OPTIONS, ...);
_server->on("/json", HTTP_GET, ...);

// Après: Ajout de HEAD
_server->on("/json", HTTP_HEAD, [](AsyncWebServerRequest* req){
  autoCtrl.notifyLocalWebActivity();
  AsyncWebServerResponse* response = req->beginResponse(200, "application/json", "");
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  response->addHeader("Access-Control-Allow-Origin", "*");
  req->send(response);
});
```

### 2. Suppression de l'appel distant bloquant
**Fichier**: `src/web_server.cpp` - Endpoint `/dbvars`

#### Avant (❌ BLOQUANT):
```cpp
// Si pas de cache valide, tenter le fetch distant avec timeout réduit
if (!ok && !remoteFetchInProgress) {
  remoteFetchInProgress = true;
  ok = autoCtrl.fetchRemoteState(src); // ⚠️ APPEL HTTP BLOQUANT 5-6 SECONDES
  remoteFetchInProgress = false;
}
```

#### Après (✅ NON-BLOQUANT):
```cpp
// OPTIMISATION: Utiliser UNIQUEMENT le cache flash - JAMAIS d'appel distant bloquant
// Les appels distants doivent être faits de manière asynchrone dans la tâche d'automatisation
String cached;
if (config.loadRemoteVars(cached) && cached.length() > 0) {
  auto err = deserializeJson(src, cached);
  if (!err) {
    ok = true;
    Serial.println("[WebServer] /dbvars: Using flash cache (fast path)");
    
    // Mettre à jour le cache mémoire pour accélérer les prochains appels
    cachedSrc = src;
    lastCacheUpdate = now;
    cacheValid = true;
  }
}
```

**Architecture maintenant**:
- ✅ Tâche d'automatisation: Fait les appels distants de manière asynchrone (en arrière-plan)
- ✅ Endpoint HTTP `/dbvars`: Lit **uniquement** le cache (rapide, <10ms)

### 3. Optimisation des timeouts clients
**Fichier**: `data/shared/websocket.js`

```javascript
// Avant: 6000ms
fetch('/json', { signal: AbortSignal.timeout(6000) })

// Après: 3000ms (suffisant maintenant que le serveur répond vite)
fetch('/json', { signal: AbortSignal.timeout(3000) })
```

Réduit de 6s à 3s pour:
- `/json`
- `/dbvars`
- `/server-status`

## 📊 Résultats attendus

### Latence
| Endpoint | Avant | Après (Cible) |
|----------|-------|---------------|
| `/json` (HEAD) | 500 Error | 200 OK (~10ms) |
| `/json` (GET) | Timeout/Lent | <100ms |
| `/dbvars` | 5-6 secondes | <50ms (cache) |
| `/server-status` | 5612ms | <200ms |

### WebSocket
- ✅ Connexion stable dès le premier essai
- ✅ Pas de tentatives multiples
- ✅ Latence < 100ms (cible du projet)

### Interface Web
- ✅ Chargement initial rapide (<3s)
- ✅ Pas de timeouts
- ✅ Affichage immédiat des données

## 🔍 Points de vérification

### Logs série à surveiller
```
[Web] 📊 /json request from ... - Heap: ... bytes
[Web] 📤 Sending ... JSON (... bytes)
[WebServer] /dbvars: Using flash cache (fast path)  // ✅ Bon signe
[DIAGNOSTIC] Serveur status - Latence: XXXms       // Doit être < 500ms
```

### Logs console web à surveiller
```javascript
[INIT] ✅ Données capteurs chargées via HTTP
[INIT] ✅ Variables BDD chargées via HTTP
[DIAGNOSTIC] Serveur status - Latence: XXXms, Heap: ... bytes
[WEBSOCKET] WebSocket connecté sur le port 81
```

### Indicateurs de succès
1. ✅ Aucune erreur HTTP 500
2. ✅ Aucun timeout sur `/json` ou `/dbvars`
3. ✅ Latence `/server-status` < 500ms (idéalement < 200ms)
4. ✅ WebSocket connecté dès la première tentative
5. ✅ Interface web réactive

### Indicateurs d'échec (à investiguer)
1. ❌ Toujours des timeouts → Problème de surcharge CPU ou watchdog
2. ❌ Latence > 1000ms → Vérifier les tâches FreeRTOS et priorités
3. ❌ WebSocket instable → Vérifier la charge réseau

## 📝 Architecture mise à jour

### Flux de données pour `/dbvars`
```
┌─────────────────────────────────────────────────────────────┐
│                   TÂCHE AUTOMATISATION                       │
│  (Asynchrone, en arrière-plan, toutes les 30s)              │
│                                                              │
│  1. Appel distant fetchRemoteState()                        │
│  2. Mise à jour du cache Flash (NVS)                        │
│  3. Mise à jour du cache mémoire                            │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│                   ENDPOINT HTTP /dbvars                      │
│  (Synchrone, doit être RAPIDE <50ms)                        │
│                                                              │
│  1. Lecture cache mémoire (30s validité)                    │
│  2. Fallback: Lecture cache Flash (NVS)                     │
│  3. Formatage et envoi JSON                                 │
│  ❌ JAMAIS d'appel distant                                  │
└─────────────────────────────────────────────────────────────┘
```

## 🚀 Prochaines étapes

1. ✅ Compiler le firmware
2. ✅ Flasher l'ESP32
3. ✅ Monitoring 90 secondes (règle du projet)
4. ✅ Analyser les logs (priorité: latence, erreurs HTTP)
5. ✅ Vérifier le WebSocket (stabilité, latence)
6. ✅ Tester l'interface web (chargement, réactivité)

## 📚 Références

- Règles du projet: Latence WebSocket < 100ms
- Architecture Phase 2: Séparation tâches/endpoints
- Cache Strategy: Flash → Mémoire → Réseau (uniquement en background)

---

**Note**: Ces corrections suivent les principes du projet:
- ❌ **Jamais bloquer** le thread principal > 3 secondes
- ✅ **Toujours** utiliser des caches pour les endpoints HTTP
- ✅ **Déléguer** les opérations longues aux tâches asynchrones

