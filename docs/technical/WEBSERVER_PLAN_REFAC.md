# Plan de refactorisation `web_server.cpp`

## Objectifs
- **Découpler** le transport HTTP/WebSocket de la logique métier (`Automatism`, e-mails, NVS).
- **Réduire la fragmentation** en remplaçant les usages risqués de `String`/`DynamicJsonDocument` par des buffers/statuts centralisés.
- **Clarifier la maintenance** en regroupant les routes par domaine (UI statique, API REST, diagnostics, contrôles actions) avec des interfaces explicites.

## Architecture cible

### 1. Carcasse transport (`WebServerManager`)
- Conserver la responsabilité de créer/initialiser `AsyncWebServer` et `realtimeWebSocket`.
- Déléguer l’enregistrement des routes à des modules dédiés :
  - `UiRoutes` (streaming SPA, assets LittleFS, fallbacks).
  - `ControlRoutes` (ancien `/action`, `/api/wakeup`, triggers nourrissage/relais).
  - `StatusRoutes` (JSON capteurs, `/wifi/status`, `/server-status`, `/debug-logs`).
  - `ConfigRoutes` (`/api/remote-flags`, gestion NVS WiFi, etc.).
- Chaque module expose `registerRoutes(AsyncWebServer&, WebServerContext&)`.
- `WebServerContext` contient les dépendances (références sur `Automatism`, `Mailer`, `SystemSensors`, `SystemActuators`, `ConfigManager`, etc.) et des helpers communs (`safeSendJson`, `sendEmailTask`, `JsonBufferPool`).

### 2. Services métier
- **NotificationService** : encapsuler `sendManualActionEmail` + gestion asynchrone (`xTaskCreate`), quotas de tâches, logs.
- **AutomationServiceAdapter** : regrouper les appels `autoCtrl.manualFeed*`, `sendFullUpdate`, `notifyLocalWebActivity` pour éviter duplication/couplage.
- **JsonResponseBuilder** : produire des réponses JSON réutilisables (status capteurs, diagnostics) en s’appuyant sur `jsonPool` ou buffers statiques.

### 3. Gestion mémoire
- Centraliser les seuils (heap min) dans `WebServerLimits` (`STREAM_MIN_HEAP`, `JSON_MIN_HEAP`, etc.).
- Remplacer les conversions `String` répétées par `snprintf` dans buffers partagés (`char[]`) pour les réponses légères.
- Contrôler l’utilisation de `DynamicJsonDocument` : privilégier `jsonPool.acquire()` avec tailles typées (512/1024) et fallback minimal.

## Plan d’exécution

1. **Refactor interne sans extraction** (phase 1)
   - Introduire `WebServerContext` et regrouper les helpers (`safeSendJson`, `sendManualActionEmail`, seuils mémoire).
   - Simplifier `/action` en déléguant la logique nourrissage/relais à un service séparé.

2. **Modules de routes** (phase 2)
   - Extraire `UiRoutes` et `StatusRoutes` (lecture seule) vers fichiers dédiés.
   - Poursuivre avec `ControlRoutes` et `ConfigRoutes`, en injectant les services nécessaires.

3. **Services métier** (phase 3)
   - Créer `NotificationService` (gestion e-mails + tâches async) et `AutomationServiceAdapter`.
   - Mettre à jour les routes pour utiliser les services (réduction du couplage).

4. **Nettoyage / tests** (phase 4)
   - Harmoniser la journalisation (`Serial.printf`) et les messages `EventLog`.
   - Documenter dans `docs/technical` l’architecture mise en place.

## Stratégie de tests

### Automatisable (post-refactor)
- **Compilation PlatformIO** (`pio run`) et analyse taille binaire (`pio run -t size`).
- **Tests HTTP** via scripts Python/curl :
  - `GET /`, `GET /index.html`, `GET /shared/common.js` (vérifier codes 200/fallback 503).
  - `GET /json`, `GET /server-status`, `GET /wifi/status` (JSON valide, champs essentiels).
  - `GET /action?cmd=feedSmall`, `feedBig` (réponse immédiate OK, absence d’erreur).
  - `POST /api/wakeup` (payload `{"action":"status"}` et `feed`).
  - `GET/POST /api/remote-flags` (ex: `send=1&recv=0`).
- **Tests WebSocket** : script simple qui se connecte, reçoit un broadcast après `GET /json` ou `/action`.

### Matériel (sur ESP32)
- Monitorer heap avant/après routes critiques (`/json`, `/shared/...`, `/action`).
- Vérifier qu’un nourrissage déclenche toujours la tâche async (logs `feed_small_sync`).
- Test de charge manuel : multiples requêtes `/json` ouvertes pour s’assurer de l’absence de fragmentation.

### Régressions à surveiller
- Maintien des fonctionnalités actuelles (fallback HTML, stream LittleFS, e-mails nourrissage, `AutomatismPersistence`).
- Performance WebSocket (pas de latence supplémentaire lors des broadcasts).
- Restauration identique des headers CORS / Cache-Control.

Ce plan servira de feuille de route pour organiser les commits et valider progressivement la nouvelle architecture du serveur web.











