# Cartographie `web_server.cpp` – état au 2025-10-29

## Vue d’ensemble

- **Point d’entrée** : `WebServerManager::begin()` configure intégralement l’`AsyncWebServer` (port 80) et le `realtimeWebSocket`. Toutes les routes HTTP, les helpers mémoire et les callbacks métier sont définis dans cette fonction monolithique (~2 500 lignes).
- **Dépendances globales** : utilisation directe des singletons `autoCtrl`, `mailer`, `config`, `power`, `wifi`, `realtimeWebSocket`, `sensorCache`, `jsonPool`, `EventLog`, `AutomatismPersistence`, etc. Peu d’isolation entre couche HTTP et logique métier.
- **Helpers internes** :
  - `safeSendJson()` pour sécuriser l’envoi JSON (vérifie la création d’`AsyncWebServerResponse`).
  - `sendManualActionEmail()` gère l’envoi d’e-mails (vérifications heap, retry) mais vit dans `web_server.cpp`, d’où forte dépendance à `autoCtrl`/`mailer`.
  - Lambdas `serveIndexStreaming`, `serveCommonJS`, etc. encapsulent du streaming LittleFS tout en accédant à des ressources globales.

## Routes principales

### Pages et assets web
- `/` & `/index.html` : streaming du SPA depuis LittleFS, fallback HTML embarqué, journalisation mémoire (`ESP.getFreeHeap()`), notifications d’activité locale (`autoCtrl.notifyLocalWebActivity`).
- `/dashboard.js`, `/dashboard.css`, `/shared/common.js`, `/shared/websocket.js` : servi statiquement avec contrôles mémoire (refus si heap < 30 KB), headers cache-control personnalisés.
- `/optimized`, `/classic`, `/favicon.ico` : redirection/fallbacks.

### Actions temps réel
- `/action` (GET) : point critique très dense. Traite les commandes manuelles (`feedSmall`, `feedBig`, commandes relais) et enclenche des tâches FreeRTOS (`xTaskCreate`) pour e-mails + synchronisation serveur (`autoCtrl.sendFullUpdate`). Mélange de logique HTTP, contrôle d’actuateurs et suivi d’état (`AutomatismPersistence`, `SensorReadings`).
- `realtimeWebSocket.loop()` géré dans `WebServerManager::loop()` ; les callbacks WebSocket sont dans `realtime_websocket.cpp` mais déclenchés depuis `/action`/`/json`.

### API REST/JSON
- `/json` (GET/HEAD/OPTIONS) : renvoie l’état complet (capteurs, actuateurs, WiFi, flags) via `jsonPool` avec fallback `DynamicJsonDocument`. Vérifie la heap (> 50 KB).
- `/api/wakeup` (POST) : actions `status`/`feed`/`default`, appelle `autoCtrl`, met à jour WebSocket, sérialise la réponse JSON.
- `/wifi/status` : expose le statut STA/AP.
- `/api/remote-flags` (GET/POST) : lecture/écriture de flags dans `ConfigManager` via query params.
- `/debug-logs`, `/server-status` : endpoints diagnostic (heap, psram, WiFi, WebSocket, `autoCtrl`), sérialisent des blocs JSON importants.
- Routes annexes : `/json` CORS preflight, `/wakeup` GET, `/wifi/networks` (scans et manipulations NVS – non montré intégralement dans les extraits mais présent plus bas), etc.

### Gestion d’e-mails et synchronisations
- `sendManualActionEmail()` + `xTaskCreate` utilisés dans plusieurs routes pour éviter blocages (mailer + `autoCtrl.sendFullUpdate`). Les tâches asynchrones manipulent `_sensors`, `AutomatismPersistence`, `jsonPool` et la synchronisation NVS.

## Points de fragilité identifiés

- **Fonction monolithique** : `WebServerManager::begin()` cumule toute la configuration. Difficulté de test et de maintenance (lambdas capturant énormément de contexte, duplication de headers et de vérifications mémoire).
- **Responsabilités mêlées** : routes HTTP pilotent directement la logique d’automatisme (nourrissage, relais), la persistence NVS, l’envoi d’e-mails, et la diffusion WebSocket. Absence de couche service dédiée.
- **Gestion mémoire hétérogène** : mélange de `String`, `DynamicJsonDocument`, `jsonPool.acquire()`, `malloc`, etc., avec vérifications heuristiques (heap < 30/50/60 KB). Le risque de fragmentation augmente avec les réponses volumineuses (`/json`, `/debug-logs`).
- **Asynchronisme dispersé** : tâches FreeRTOS créées inline (`xTaskCreate`) sans supervision centralisée. Risque d’accumulation (`asyncTaskCount` global statique dans chaque lambda).
- **Tests difficiles** : absence d’API claire pour simuler les routes ; dépendance à l’état global (`WiFi`, `autoCtrl`, `LittleFS`).

## Dépendances externes clés

- `AsyncWebServer`, `AsyncWebServerRequest/Response`, `LittleFS`.
- `autoCtrl` (`Automatism`), `Mailer`, `ConfigManager`, `PowerManager`, `WifiManager`.
- `SensorReadings`, `sensorCache`, `jsonPool`, `AutomatismPersistence`, `realtimeWebSocket`.
- FreeRTOS (`xTaskCreate`, `vTaskDelay`, `vTaskDelete`).

## Conclusion

Le serveur HTTP combine aujourd’hui quatre responsabilités majeures :
1. **Distribution UI** (streaming fichiers + fallback),
2. **API REST** pour capteurs/actionneurs, diagnostics, configuration,
3. **Pilotage d’actionneurs/automatisme** (nourrissage, relais, synchronisation),
4. **Infrastructure** (WebSocket, e-mails, NVS).

Cette cartographie servira de base au plan de refactorisation (séparation en contrôleurs spécialisés, services métier dédiés et couche de transport claire).











