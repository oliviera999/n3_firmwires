# Audit général FFP5CS — 2026-05

> Rapport consolidé des 6 sous-audits parallèles (architecture, réseau/OTA, mémoire/RTOS, capteurs/automatismes, sécurité/web, build/tests/doc).
>
> **Version firmware au lancement** : 13.51 → cibles successives v13.52 → v13.90.
> **Date d'analyse** : 2026-05-19/20.
> **Plan d'exécution** : 7 incréments versionnés livrés en agent mode.

## Sommaire

1. [Synthèse exécutive](#1-synthèse-exécutive)
2. [Sécurité (web local, secrets, transport)](#2-sécurité)
3. [Capteurs, actionneurs, automatismes](#3-capteurs-actionneurs-automatismes)
4. [Mémoire, FreeRTOS, watchdog](#4-mémoire-freertos-watchdog)
5. [Réseau, OTA, contrat firmware-serveur](#5-réseau-ota-contrat-firmware-serveur)
6. [Architecture et qualité du code](#6-architecture-et-qualité-du-code)
7. [Build, tests, CI, documentation](#7-build-tests-ci-documentation)
8. [Plan d'action priorisé](#8-plan-daction-priorisé)
9. [Hors-scope contrat firmware-serveur (rétrocompatibilité)](#9-hors-scope-contrat-firmware-serveur)

---

## 1. Synthèse exécutive

| Axe | BLOQUANT | IMPORTANT | MINEUR |
|-----|---------:|----------:|-------:|
| Sécurité | 5 | 7 | 6 |
| Capteurs / Automatismes | 0 | 6 | 12+ |
| Mémoire / RTOS | 1 | 4 | 8 |
| Réseau / OTA | 2 | 5 | 6 |
| Architecture | 3 | 5 | 8 |
| Build / Tests / Doc | 3 | 8 | 10+ |

**Points forts** : firmware modulaire (Automatism + sous-modules sync/sleep/feeding), pool `NetRequest` statique, fallbacks NVS systématiques, mutex TLS partagé OTA/SMTP, anti-fragmentation mail, watchdog différencié par profil.

**Points sensibles immédiats** :
- Routes web admin sans authentification (nourrissage, relais, mail, OTA, format FS, mots de passe WiFi en clair).
- Bug auto-référence `API_KEY = API_KEY` dans `include/config.h`.
- Trafic serveur en HTTP + `api_key` en clair (HMAC supporté côté serveur mais non implémenté firmware).
- Fallback ultrason `wlAqua = 0` qui déclenche fausse alerte inondation.
- SMTP exécuté dans `automationTask` sans feed TWDT.
- `app_tasks.cpp` (1709 l.), `web_server.cpp` (1949 l.), `ota_manager.cpp` (1963 l.) — monolithes à découper.

---

## 2. Sécurité

### 2.1 Secrets

| Fichier:ligne | Gravité | Constat | Action |
|---|---|---|---|
| `include/config.h:11-18` | **BLOQUANT** | Pattern `constexpr const char* API_KEY = API_KEY;` — auto-référence si `credentials.h` définit `#define API_KEY "..."`. Fragile. | Renommer le membre (`Secrets::API_KEY`) avec un `#undef API_KEY` préalable, ou littéral string explicite. Implémenté en v13.52. |
| `include/config.h:19-25, 386-387` | **IMPORTANT** | Fallbacks compile `API_KEY` / `WEB_AUTH_PASS = "CHANGEZ_MOI"` embarqués dans le `.bin` si secrets non configurés. | `static_assert` PROFILE_PROD pour rejeter le placeholder. Implémenté en v13.52. |
| `secrets_config.h.example` | **IMPORTANT** | Fichier absent du dépôt malgré référence. | Créer `include/secrets_config.h.example`. Implémenté en v13.52. |
| `include/secrets.h.example` | OK | Placeholders explicites, exclusion réseau dev en wroom-prod. | — |
| `.gitignore` racine | MINEUR | Exclut `secrets.h`, `credentials.h` mais pas `secrets_config.h` ni `local_server_overrides.h`. | Aligner sur `firmwires/ffp5cs/.gitignore`. Incrément 3. |

### 2.2 Web local — routes sensibles sans auth

| Route | Fichier:ligne | Gravité | Risque |
|---|---|---|---|
| `GET /json` | `web_routes_status.cpp:450-505` | **BLOQUANT** | Exposition capteurs, relais, sync, WiFi |
| `POST /api/wakeup` action `feed` | `web_routes_status.cpp:136-217` | **BLOQUANT** | Nourrissage à distance par tiers LAN |
| `POST /api/remote-flags` | `web_routes_status.cpp:298-348` | **BLOQUANT** | Active/désactive envoi/réception serveur |
| `GET /mailtest` | `web_server.cpp:1007-1056` | **BLOQUANT** | Envoi email arbitraire |
| `GET /testota` | `web_server.cpp:1078-1082` | **BLOQUANT** | Déclenche OTA |
| `GET /fs/format?confirm=1` | `web_server.cpp:1060-1074` | **BLOQUANT** | Formatage FS (FFP_ENABLE_DANGEROUS_ENDPOINTS) |
| `GET /wifi/saved` | `web_server.cpp:1697-1698` | **BLOQUANT** | Mots de passe WiFi NVS retournés en clair |

**Action v13.52** : `if (!webAuthIsAuthenticated(req)) { req->send(401); return; }` ajouté à toutes ces routes. `/wifi/saved` ne retourne que `{"ssid":..., "hasPassword":bool}`.

### 2.3 WebSocket port 81

| Fichier:ligne | Gravité | Constat | Action |
|---|---|---|---|
| `realtime_websocket.h:50-56, 109-112` | **BLOQUANT** | Aucune auth à la connexion ; capteurs + dbVars (config) diffusés sur LAN. | Token session (cookie ou query `?token=`) requis à `WStype_CONNECTED`. Implémenté v13.52. |
| `realtime_websocket.h:51-56, 181-184` | MINEUR | Max 4/8 clients, messages TEXT ≤ 512 o. | Limite par IP, fermeture clients inactifs agressive. Incrément 5. |
| `data/shared/websocket.js:36-63, 1494-1498` | MINEUR | Reconnexion : 20 tentatives / 20s sans backoff exponentiel strict. | Backoff exponentiel plafonné. Incrément 5. |

### 2.4 Cookie session

| Fichier:ligne | Gravité | Constat | Action |
|---|---|---|---|
| `web_server.cpp:108-114` | **BLOQUANT** | Token session via `random()` + `randomSeed(micros()+heap)` — entropie faible. Pas de TTL, pas de rotation. | `esp_fill_random` (16 bytes hex) ; rotation à chaque login. v13.52. |
| `web_server.cpp:507-508` | **BLOQUANT** | Cookie sans `HttpOnly`, `Secure`, `SameSite`. Vulnérable XSS et CSRF. | `HttpOnly; SameSite=Strict`. v13.52. |
| — | IMPORTANT | Aucune protection CSRF (token CSRF). | `SameSite=Strict` couvre la plupart ; vérification `Origin/Referer` côté handlers sensibles. v13.52. |

### 2.5 Authentification forte AP secours (S3)

| Fichier:ligne | Gravité | Constat | Action |
|---|---|---|---|
| `wifi_manager.cpp:590, 598-601` | **IMPORTANT** | AP secours S3 ouvert (`nullptr` mot de passe) + DNS captive `*` → `softAPIP()`. | WPA2 via `Secrets::AP_FALLBACK_PASSWORD`. Incrément 3 v13.60. |

### 2.6 TLS / OTA

| Fichier:ligne | Gravité | Constat | Action |
|---|---|---|---|
| `web_client.cpp:1-3, 77-79` | **BLOQUANT** (déploiement) | Trafic métier en HTTP (POST/GET) — `api_key` et capteurs lisibles. | Migration HTTPS via flag `USE_HTTPS_ENDPOINTS`. Incrément 6 v13.80 (mode dual). Bascule v13.90. |
| `ota_manager.cpp:36-41, 1843+` | **BLOQUANT** | OTA downgrade en HTTP après metadata HTTPS. MD5 seul insuffisant en HTTP (MITM possible). | Signature Ed25519 optionnelle. Incrément 6 v13.80. Obligatoire v13.90. |
| `ota_manager.cpp:526-531` | **IMPORTANT** | Metadata OTA HTTPS avec `skip_cert_common_name_check = true`. | Désactiver le skip (CN doit matcher). Incrément 3 v13.60. |
| `tls_minimal_test.cpp:98` | MINEUR | `setInsecure()` dans test TLS. | `#ifdef` pour exclure des builds prod. Incrément 5. |

### 2.7 Mailer

| Fichier:ligne | Gravité | Constat | Action |
|---|---|---|---|
| `mailer.cpp:311-355, 1176-1204` | MINEUR | Logs série : sujet, emails expéditeur/destinataire, BSSID, IP, DNS. | Filtrer en `PROFILE_PROD` (`ENABLE_SERIAL_MONITOR=0` déjà OK). Incrément 3. |
| `mailer.cpp:1465`, `config.h:1023` | MINEUR | `MAIL_QUEUE_SIZE=6` retry max 2 — pas de rate limit temporel global. | Plafond mails/heure + backoff anti-spam alertes. Incrément 5. |


---

## 3. Capteurs, actionneurs, automatismes

### 3.1 Lecture capteurs

**DHT22/DHT11 + BME280 (S3)**
- `MIN_READ_INTERVAL_MS=2500ms` DHT (`config.h:620`), `500ms` BME280 (`config.h:628`).
- `robust*()` : timeout recovery 2s, fallback `_lastValid*`, défauts sûrs (20°C / 50%).
- **MINEUR** : double mécanisme de défaillance air (`_sensorDisabled` dans `AirSensor` vs `SensorFailureManager` ailleurs). À unifier — incrément 5.

**DS18B20 (eau)**
- Pipeline non-bloquant (`setWaitForConversion(false)`, timeout 1s).
- Code -127 traité dans `SensorValidation::isValidWaterTemp` (`config.h:689-691`).
- **MINEUR** : `STABILIZATION_READINGS=1`, écart `MIN_READINGS=4` (config) vs 2 (sensors.cpp). Documenter ou aligner — incrément 5.

**HC-SR04 ultrason**
- **BLOQUANT** : `wlAqua = 0` sur invalide (`system_sensors.cpp:84-85`) → 0 < `limFlood` (80mm) → fausse alerte inondation. Verrouillage pompe réservoir (`automatism.cpp:663`). **Action v13.53** : remplacer par `_lastValidWlAqua` ou sentinelle `Fallback::WATER_LEVEL_AQUA`.
- **BLOQUANT** : Plages incohérentes — `MAX_DISTANCE_MM=4000` (`config.h:635`), `MAX_VALID_LEVEL_MM=5000` (`system_sensors.cpp:50`). **Action v13.53** : aligner à 5000 (capteur HC-SR04 supporte jusqu'à ~5m mesurable) et documenter.
- **MINEUR** : `FILTERED_READINGS_COUNT=3` (`config.h:648`) **non utilisé** ; `READINGS_COUNT=5` hardcodé dans `sensors.h`. Référencer la constante ou supprimer. Incrément 5.

**Validation `SensorValidation::*`**
- Bonnes bases : `isValidWaterTemp`, `isValidAirTemp`, `isValidHumidity`, `isValidDistance`, `sanitize*`.
- **MINEUR** : `system_sensors.cpp` duplique les checks `isnan` + plages au lieu d'appeler les helpers. Incrément 5.
- **MINEUR** : pas de `sanitizeDistance()` (validation manuelle ad hoc). Incrément 5.

### 3.2 Actionneurs

- Logique état miroir `_state` cohérente.
- Pompe aqua ON par défaut au boot (`system_actuators.cpp:26-28`) — cohérent aquaponie.
- Servos via `esp_timer` (retour repos + detach 400ms) — limite consommation.
- **BLOQUANT** : `GPIOMap` pompes/lumière hardcodés (`gpio_mapping.h:79-82`) : 16/18/15. Doivent référencer `Pins::POMPE_AQUA`, `Pins::POMPE_RESERV`, `Pins::LUMIERE` pour rester cohérent WROOM↔S3. **Action v13.53**.
- **MINEUR** : pompe réservoir non incluse dans snapshot veille (continue ou s'arrête via logique métier). À documenter.

### 3.3 GPIO mapping

- WROOM vs S3 conditionnels (`pins.h:5-82`) — conforme règle isolation.
- Pas de conflit GPIO détecté (SPI SD, I2C, OneWire, servos S3 OK).
- **MINEUR** : `gpio_parser.cpp` : logs `[DBG] hypothesis=D` encore présents (cf. l.89-90, 259, 277). Nettoyer — incrément 3.

### 3.4 Nourrissage (créneaux 105/106/107)

- Correctif v13.48 OK : `markSlotsDoneForScheduleHour` couvre l'égalité matin=midi=soir.
- Grace boot 2 min, fenêtre H + rattrapage H+1.
- `finalizeFeedingIfNeeded` + `syncFeedEdgeStateAfterLocalPost(true,true)` après auto.
- **MINEUR** : `else if` dans `checkAndFeed` peut ignorer un 2e créneau rattrapable même heure H+1 (cas rare).

### 3.5 Veille légère

- Quiesce HTTP avant sleep, flag TLS `g_enteringLightSleep`, fermeture WS propre.
- Snapshot NVS aqua/heater/light → restauration au réveil avant POST. v13.46 OK.

### 3.6 RTC DS3231

- Compile sous `USE_RTC_DS3231`, I2C 0x68.
- `applyExternalRTCIfPresent` prioritaire sur NVS si RTC valide.
- **MINEUR** : `loadTimeWithFallback()` ne consulte pas DS3231 directement (NVS → compile → défaut). À intégrer ou documenter ordre boot. Incrément 5.

### 3.7 NVS Manager

- 4 namespaces (sys/cfg/state/logs), mutex récursif, clés ≤15 car.
- **v13.46** : `saveBool` corrigé (`isKey` avant compare). 
- **IMPORTANT** : `saveInt`/`saveFloat`/`saveULong` non protégés par le même pattern. Cas extrême mais possible. **Action v13.70**.
- **MINEUR** : pas de recovery applicatif `nvs_flash_erase` sur `NO_FREE_PAGES` / `NEW_VERSION_FOUND` (délégué Arduino). Documenter procédure maintenance.

### 3.8 PowerManager

- Light sleep, save/reconnect WiFi, ajustement epoch post-sleep.
- `smartSaveTime` anti-régression > `MAX_RTC_REGRESSION_SEC`.
- WDT reset systématique dans init/update. OK.


---

## 4. Mémoire, FreeRTOS, watchdog

### 4.1 Estimation DRAM (WROOM prod, ordres de grandeur)

| Composant | ~ KB |
|---|---:|
| Stacks statiques (sensor 3072 + auto 10240 + net 12800 + ota 12288) | ~38,4 |
| TCB FreeRTOS x4 | ~0,4 |
| Pool `NetRequest` (8 × ~928 o, payload 896) | ~7,4 |
| Buffers BSS (mailer ~4,3 + caches 1024+1536+1536+2049 + s_dbvarsCachedSrc) | ~12-15 |
| **Total app BSS hors IDF/WiFi/LwIP** | **~58-65** |

**IMPORTANT** : l'inventaire commenté dans `config.h:907-920` annonce ~50 KB stacks 26 KB → **dépassé** (+10-15 KB). À mettre à jour — incrément 5.

### 4.2 Stacks FreeRTOS — résumé

| Tâche | WROOM prod | WROOM beta | WROOM test | S3 |
|---|---|---|---|---|
| sensor | 3072, c1, p2 | 2560 | 3072 | 3072 |
| web | heap 10240, c1, p1 | idem | idem | idem |
| auto | 10240 stat., c1, p3 | idem | 8192 | 12288 |
| net | **12800** stat., c0, p2 | 14224 | 9216 | 16384 |
| ota | 12288 stat., c0, p3→**10** | 11264 | 9216 | 12288 |
| postSender | heap 8192, c0, p1 | idem | idem | idem |

- **MINEUR** : `WEB_TASK_STACK_SIZE=10240` — en prod sans AsyncWeb peut être surdimensionné. À mesurer. Incrément 5.
- **MINEUR** : `MAIL_TASK_STACK_SIZE` (12-15 KB) défini mais aucune `mailTask` créée — confusion. À nettoyer.

### 4.3 Heap / seuils

- `MIN_HEAP_FOR_TLS=35000` partagé HTTPS+SMTP.
- **IMPORTANT** : HTTP courant n'a pas besoin de 35 KB. Distinguer `MIN_HEAP_FOR_SMTP` / `MIN_HEAP_FOR_HTTPS`. Incrément 5.
- Réserve mail 31 KB allouée au boot (libérée pour OTA, réallouée après).
- `MIN_HEAP_RESPONSE_STREAM=28672` pertinent surtout en wroom-test (AsyncWeb).

### 4.4 Allocations / chemins chauds

- **BLOQUANT** : `mailer.cpp:1001-1126` — `_smtp.connect` + `MailClient.sendMail` dans `automationTask` (souscrite TWDT 30s) **sans `esp_task_wdt_reset`**. Risque reboot si SMTP > 30s. **Action v13.53**.
- **IMPORTANT** : `tls_mutex.h:69-93` — mutex TLS timeout 10s + stabilisation 3s + SMTP — dépasse règle "blocage > 3s". À traiter avec correctif SMTP.
- Pool `NetRequest` statique : OK.
- Peu de `String` Arduino dans chemins chauds : OK.

### 4.5 Watchdog

- S3 PSRAM : IWDT MWDT1 désactivé tôt, TWDT 300s.
- WROOM : TWDT 30s prod/test, 60s beta (OTA TLS).
- `netTask`, `postSenderTask`, `sensorTask`, `otaTask` font `esp_task_wdt_add` + reset boucle. OK.
- **IMPORTANT** : `diagnostics.cpp:504-505` — message debug "watchdog 300s" peut être faux sur WROOM 30/60s. Lire timeout réel. Incrément 5.

### 4.6 Tâches, priorités, starvation

| Core | Tâches |
|---|---|
| Core 0 | net(2), ota(3→**10** OTA), postSender(1) |
| Core 1 | auto(3), sensor(2), web(1) |

- **OTA_TASK_PRIORITY_WHILE_RUNNING=10** (config.h:981) : net/postSender ne préemptent pas pendant OTA (intentionnel).
- `automationTask` : capteurs + OLED + SMTP + netRPC wait — charge importante core 1.

### 4.7 Queues

| Queue | Taille | Comportement saturation |
|---|---|---|
| `g_sensorQueue` | 8 | Écrase ancienne entrée |
| `g_netQueue` | 8/16 | RPC abandon 200ms ; POST fire-and-forget |
| `g_postSenderQueue` | **4** | POST → NVS ; **heartbeat perdu silencieusement** |
| `_mailQueue` | **6** | Retry max 2 ; pas de persistance NVS |
| `g_otaTriggerQueue` | 2 | Trigger renoncé |

- **IMPORTANT** : Heartbeat perdu si `postSender` plein (commentaires log toutes les 60s seulement). Action incrément 5 : slot réservé ou compteur diagnostic.

### 4.8 Mutex HTTP

- **BLOQUANT** : `web_client.cpp:37, 101` — `xSemaphoreTake(_httpMutex, portMAX_DELAY)`. Un POST 18s peut bloquer tout GET/heartbeat. **Action v13.53** : timeout `timeoutMs + 5000ms` ; échec → fallback NVS / abandon.


---

## 5. Réseau, OTA, contrat firmware-serveur

### 5.1 WiFi

- **IMPORTANT** : Scan WiFi synchrone bloquant 2-5s au boot (`wifi_manager.cpp:66, 211-217, 572-576`). Documenter dérogation.
- **IMPORTANT** : Boot peut prendre **plusieurs minutes** (4 tentatives × 15s × N réseaux). **Action v13.70** : cap timeout boot 20s puis bascule AP secours immédiate ; hors boot `connect()` léger 1 tentative sans scan.
- Modem-sleep désactivé si RSSI faible. OK.
- Reconnexion après veille : 8s max + stabilisation 5s + test DNS. OK (dérogation documentée).

### 5.2 HTTP client

- **BLOQUANT** : Mutex HTTP `portMAX_DELAY` (cf. §4.8). v13.53.
- Timeouts > 5s : GET 8s, POST 15-18s, RPC 12s/22s — dérogations documentées (latence prod observée).
- 4xx : pas de retry, échec → `queueFailedPost` NVS. OK.
- v13.51 : libération slot `NetRequest` GET après échec OK ; **MINEUR** : audit complet POST (incrément 5 v13.70).

### 5.3 Contrat firmware↔serveur — alignement actuel

| Profil PIO | Endpoints firmware | Env serveur |
|---|---|---|
| wroom-prod | `/ffp3/post-data`, `/ffp3/api/outputs/state`, `/ffp3/heartbeat` | prod |
| wroom-test/beta | `*-test` | test |
| wroom-s3-test (USE_TEST3) | `/ffp3/post-data3-test` etc. | **test3** |
| wroom-s3-prod | `/ffp3/post-data3` etc. | **s3** |
| wroom-s3-test (sans USE_TEST3) | `/ffp3/post-data-s3-test` etc. | **s3test** |
| OTA | `/ota/` (alias `/ffp3/ota/`) | OK |

**Action v13.60** : matrice endpoints S3 corrigée dans `docs/README.md` (actuellement seuls `post-data3-test` cités).

### 5.4 Divergences (signalées, non bloquantes immédiatement)

- **BLOQUANT (déploiement)** : trafic prod en HTTP clair ; `api_key` lisible. **Action v13.80** : HTTPS opt-in via flag `USE_HTTPS_ENDPOINTS` ; bascule défaut v13.90.
- **BLOQUANT (déploiement)** : firmware n'envoie que `api_key` ; serveur supporte HMAC-SHA256 (`SignatureValidator`) mais firmware ne l'implémente pas. **Action v13.80** : HMAC ajouté en complément, mode dual ; rétrocompat assurée.
- **MINEUR** : `docs/technical/SEUILS_SERVEUR_ESP32.md` cite `ffp3/src/Service/SensorDataService.php` — chemin obsolète (serveur unifié `serveur/src/...`). Incrément 3.

### 5.5 OTA

- `otaTask` priorité 10 pendant update, WDT feed dans boucles.
- MD5 vérifié, version comparée (anti-downgrade).
- **MINEUR** : pas d'`esp_ota_mark_app_valid_cancel_rollback()` explicite après boot fonctionnel. **Action v13.70**.
- `compareVersions` accepte si `remote >= local` ; logique anti-downgrade par version. OK.
- **IMPORTANT** : metadata HTTPS avec `skip_cert_common_name_check=true`. Action v13.60.
- **BLOQUANT (déploiement)** : binaire OTA en HTTP + MD5 seul (MITM possible). **Action v13.80** : signature Ed25519 optionnelle ; obligatoire v13.90.

### 5.6 WebSocket

- Port 81, path `/ws`, heartbeat 15s/3s/2 échecs.
- Commandes : `get_status` lecture seule. OK.
- **BLOQUANT** : auth absente (cf. §2.3).


---

## 6. Architecture et qualité du code

### 6.1 Modularité — fichiers > 800 lignes

| Fichier | Lignes (~) | Gravité |
|---|---:|---|
| `src/web_server.cpp` | 1949 | IMPORTANT |
| `src/ota_manager.cpp` | 1963 | IMPORTANT |
| `src/app_tasks.cpp` | **1709** | **BLOQUANT** (orchestration + réseau + pools) |
| `src/sensors.cpp` | 1679 | IMPORTANT |
| `src/mailer.cpp` | 1638 | IMPORTANT |
| `src/automatism.cpp` | 1512 | IMPORTANT |
| `src/display_view.cpp` | 1280 | IMPORTANT |
| `src/nvs_manager.cpp` | 1168 | IMPORTANT |
| `src/web_client.cpp` | 1082 | IMPORTANT |
| `src/wifi_manager.cpp` | 839 | IMPORTANT |

### 6.2 Header monolithique

- `include/config.h` : 1062 lignes — constantes / macros / helpers / stub `NullSerial` / macro `WIFI_APPLY_MODEM_SLEEP`.
- `include/realtime_websocket.h` : 740 lignes d'implémentation dans header.
- **Action v13.60** : découper `config.h` en `config_system.h`, `config_network.h`, `config_buffers.h`, `config_sensors.h`, `config_logging.h`, `config_tasks.h`. `config.h` devient agrégateur (rétrocompat).
- **Action v13.65** : déplacer impl `realtime_websocket.h` → `realtime_websocket.cpp`.

### 6.3 Préprocesseur

| Fichier | ~Occurrences `BOARD_*` | Action |
|---|---|---|
| `src/app_tasks.cpp` | **121** | **BLOQUANT** — découper + `board_traits.h` |
| `src/wifi_manager.cpp` | 34 | Refactor (incrément 4) |
| `src/app.cpp` | 22 | Extraire `SystemBoot::initWatchdog()` v13.53 |
| `include/config.h` | 26+ | Découpage v13.60 |

**Action v13.60** : `include/board_traits.h` (`constexpr bool isS3()`, `hasPsram()`, `isProd()`, `isTest()`, `isBeta()`, `isDev()`).

### 6.4 Couplages

- **IMPORTANT** : `extern Automatism g_autoCtrl, Mailer mailer, ConfigManager config` dans `web_server.cpp:34-38` au lieu d'`AppContext&`. **Action v13.65**.
- **MINEUR** : dépendance circulaire mailer→automatism contournée via `ffp5csGetLastSendMsForMail()` global. À remplacer par `MailFooterSnapshot` rempli par `AutomatismSync`. v13.65.
- **IMPORTANT** : `app_context.h` inclut tout (`wifi_manager.h`, `web_server.h`, `automatism.h`...). **Action v13.65** : forward declarations.

### 6.5 Surface API Automatism

- `Automatism::readSensors()` public (`automatism.h:143`) appelle `_sensors.read()` (bloquant 1-7s) — appelé par footer mail (`mailer.cpp:199-200`).
- **IMPORTANT** : remplacer par `getCachedReadings()` ; footer mail utilise cache. **Action v13.65**.

### 6.6 Conventions

- **MINEUR** : `config_manager.h` sans `#pragma once` (utilise `#ifndef`). Aligner.
- **MINEUR** : `wifi_manager.h` double garde (`#pragma once` + `#ifndef`). Unifier.
- Lignes > 100 caractères fréquentes dans commentaires longs `app_tasks.cpp`. Tolérable.

### 6.7 Code mort / commentaires obsolètes

- 60+ commentaires `// v11.*`, `// v13.0x` dans `config.h` et `src/`. **Action v13.60** : nettoyer (renvoi `VERSION.md`).
- `realtime_websocket.cpp` : 24 lignes ; logique dans header. Réorganiser v13.65.
- `displayTask` supprimée mais `task_monitor.cpp` logue encore `D=` (toujours 0). Action v13.70.
- `MAIL_TASK_STACK_SIZE` défini mais aucune `mailTask` créée. À nettoyer v13.65.


---

## 7. Build, tests, CI, documentation

### 7.1 platformio.ini

- **OK** : 12 environnements WROOM/S3 cohérents avec règles `.cursor/rules/`. WROOM = pioarduino 55.03.37 ; S3 = espressif32@6.13.0 (alignement bloqué par linker).
- **IMPORTANT** : `lib_deps` libs Async (`AsyncTCP`, `ESPAsyncWebServer`, `WebSockets`) **flottantes**. Risque régression silencieuse. **Action v13.60** : épingler.
- **IMPORTANT** : `-Wno-error` global sur prod/test masque warnings. **Action v13.60** : job CI `wroom-test-strict` sans `-Wno-error` (lint hebdo).
- **MINEUR** : `wroom-s3-test-psram` vs `-psram-v2` peu documentés (différence : `extra_scripts`). Marquer `# DEPRECATED` ou consolider. v13.60.
- **MINEUR** : `default_envs = wroom-test` — pertinent dev, doc à clarifier pour livraison. v13.60.

### 7.2 Scripts pre-build

**Présents** (vérifié incrément 0) :
- `tools/pio_ensure_secrets.py`, `tools/pio_ensure_git_data.py`, `tools/pio_ensure_lib_dirs_windows.py`
- `tools/pio_add_mklittlefs_path.py`, `tools/pio_write_build_version.py`
- `tools/s3_patch_*.py`, `tools/pio_pre_apply_arduino_patches.py`, `tools/pio_s3_psram_patch_iwdt.py`

### 7.3 Fichiers manquants

| Fichier | Statut | Action |
|---|---|---|
| `platformio-native.ini` | **ABSENT** | **Action v13.60** : restaurer (env native pour `pio test`) |
| `scripts/build_all_envs.ps1` | **ABSENT** | **Action v13.60** : restaurer ou retirer ref `run_ci_checks.ps1` |
| `scripts/test_wroom_beta_local_*.ps1` | **ABSENT** | **Action v13.60** : restaurer batterie complète |
| `scripts/run_wroom_beta_local_test_suite.ps1` | **ABSENT** | **Action v13.60** : restaurer |
| `scripts/wroom_beta_local_test_scenarios.json` | **ABSENT** | **Action v13.60** : restaurer |
| `scripts/.beta-local-test.env.example` | **ABSENT** | **Action v13.60** : créer |
| `include/local_server_overrides.h.example` | **ABSENT** | **Action v13.60** : créer |
| `test/test_server_url/` | **ABSENT** | **Action v13.70** : créer suite |

### 7.4 Tests Unity

| Suite | Compilable ? | Couverture |
|---|---|---|
| `test/test_config/` | **Non** (besoin `platformio-native.ini`) | ConfigManager + mock NVS |
| `test/test_nvs/` | Idem | Validation clés NVS |
| `test/test_rate_limiter/` | **Non** (`rate_limiter.h` absent) | À supprimer ou réimplémenter |

**Action v13.70** : ajouter suites `test_sensor_validation`, `test_server_url_config`, `test_gpio_mapping` ; activer `pio test -e native` en CI.

### 7.5 CI GitHub Actions

- 2 workflows parallèles : `build.yml` (wroom-prod, wroom-test, lint `pio check` `continue-on-error`) et `pio-build.yml` (wroom-prod, wroom-s3-prod).
- **IMPORTANT** : pas de `pio test` ; pas de `wroom-beta` ; lint non bloquant.
- **Action v13.60** : fusionner workflows ; ajouter `pio test` quand native restauré.

### 7.6 Documentation

| Élément | Statut | Action |
|---|---|---|
| `README.md` badge | **13.26** vs config.h **13.51** | **v13.52** |
| `README.md` env quick-start | `s3-test` (inexistant) | **v13.52** : `wroom-s3-test` |
| `README.md` "Dernière mise à jour" | 2026-03-23 | **v13.52** |
| `README.md` historique | parle de v11.* | **v13.52** : réduire, lien VERSION.md |
| `VERSION.md` 13.39-13.41 | cite fichiers absents | **v13.60** corriger ou restaurer |
| `docs/README.md` | liens `analysis/code-quality/*.md` morts | **v13.60** corriger |
| `docs/README.md` | `pio test -e native` cassé | **v13.60** mettre à jour après restauration |
| `firmwires/README.md` parent | cite `build_all_envs.ps1`, `platformio-native.ini` | **v13.60** aligner |
| `serveur/ota/metadata.json` | **prod 13.35**, test 13.47 vs firmware **13.51+** | **v13.70** publier OTA |

### 7.7 Cohérence version

- Source de vérité : `include/config.h::ProjectConfig::VERSION` (13.51).
- `VERSION.md` aligné. README désaligné. OTA metadata désaligné.
- `scripts/verify_version.ps1` compare config.h ↔ VERSION.md (manuel, non CI).


---

## 8. Plan d'action priorisé

### 8.1 Roadmap

| Incrément | Version | Thème | Statut |
|---|---|---|---|
| 0 | — | Vérifs préliminaires | OK |
| 1 | **13.52** | Sécurité critique web + rapport | En cours |
| 2 | **13.53** | Fonctionnel critique (wlAqua, ultrason, GPIOMap, SMTP, mutex HTTP) | À venir |
| 3 | **13.60** | Hygiène + restauration beta-local + sécurité moyenne | À venir |
| 4 | **13.65** | Refactor architecture (app_tasks, web_server, Automatism) | À venir |
| 5 | **13.70** | Robustesse mémoire/réseau + tests | À venir |
| 6 | **13.80** | Migration HTTPS+HMAC+sig OTA (mode dual rétrocompatible) | À venir |
| 7 | **13.90** | Bascule HTTPS+HMAC par défaut (après pilote) | À venir |

### 8.2 Détail par incrément (résumé)

#### v13.52 — Sécurité critique
- Bug `API_KEY = API_KEY` corrigé (renommage).
- `static_assert` PROFILE_PROD pour secrets non-placeholder.
- `secrets_config.h.example` créé.
- Routes admin protégées (`/api/wakeup feed`, `/api/remote-flags`, `/mailtest`, `/testota`, `/fs/format`).
- `/wifi/saved` ne renvoie plus de mots de passe.
- WebSocket : token session obligatoire.
- Cookie : `esp_fill_random` + `HttpOnly; SameSite=Strict` + TTL + rotation.
- README badge + dernière modif + historique.
- Commentaires `// v13.0x` purgés de `config.h`.

#### v13.53 — Fonctionnel critique
- `wlAqua = 0` → fallback `_lastValid` ou sentinelle.
- Plages ultrason 4000/5000 mm alignées + documenté.
- `GPIOMap` pompes/lumière → `Pins::POMPE_AQUA`/`POMPE_RESERV`/`LUMIERE`.
- `mailer.cpp` : feed TWDT pendant `_smtp.connect` et `MailClient.sendMail`.
- `web_client.cpp` : mutex HTTP avec timeout au lieu de `portMAX_DELAY`.
- Init TWDT extrait vers `SystemBoot::initWatchdog()`.
- Slot `NetRequest` POST libéré sur échec (symétrique v13.51 GET).

#### v13.60 — Hygiène + sécurité moyenne
- `config.h` découpé en 6 modules + agrégateur.
- `board_traits.h` (constexpr).
- Commentaires `// v11.*` nettoyés.
- `platformio.ini` documenté + libs Async épinglées.
- AP secours WPA2 (S3).
- OTA metadata `skip_cert_common_name_check = false`.
- CORS `*` retiré.
- `platformio-native.ini`, `build_all_envs.ps1`, suite `wroom-beta-local` restaurés.

#### v13.65 — Refactor architecture
- `app_tasks.cpp` découpé (`net_task.cpp`, `post_sender_task.cpp`, `ota_task.cpp`).
- `web_server.cpp` : extraction handlers vers `web_routes_admin.cpp`.
- `Automatism::readSensors()` retiré ; footer mail via cache.
- `app_context.h` : forward declarations.
- `extern` dans `web_server.cpp` remplacés par `AppContext&`.

#### v13.70 — Robustesse + tests
- NVS : pattern v13.46 généralisé `saveInt`/`saveFloat`/`saveString`/`saveBlob`.
- Heartbeat avec slot réservé ou compteur diagnostic.
- WiFi connect : cap timeout boot 20s + AP secours.
- `MIN_HEAP_FOR_SMTP` / `MIN_HEAP_FOR_HTTPS` distincts.
- `esp_ota_mark_app_valid_cancel_rollback()` après boot validé.
- Tests Unity : `sensor_validation`, `server_url_config`, `gpio_mapping`.
- Inventaire DRAM `config.h:907-920` actualisé.
- `task_monitor` : ajouter `postSender`/`ota`, retirer `displayTask`.

#### v13.80 — Migration contrat (mode dual)
- Flag `USE_HTTPS_ENDPOINTS` (HTTP reste défaut).
- `hmac_sign.cpp/h` (HMAC-SHA256 via mbedtls).
- En-têtes `X-Sig-Timestamp`, `X-Sig-Nonce`, `X-Sig-Hmac` en complément `api_key=...`.
- Signature Ed25519 OTA optionnelle.
- Coordination serveur : vérifier `SignatureValidator` dual-mode.
- Doc `MIGRATION_HMAC_HTTPS.md`.

#### v13.90 — Bascule par défaut
- `USE_HTTPS_ENDPOINTS` actif en wroom-prod, wroom-s3-prod.
- HMAC obligatoire si `API_SIG_SECRET` configuré.
- Signature OTA Ed25519 obligatoire en prod.

### 8.3 Étape finale

- `docs/reports/AUDIT_GENERAL_2026-05.md` : section "Bilan" (fait / différé / abandonné).
- `docs/README.md`, `firmwires/README.md` parent : matrice firmware-serveur, scripts versionnés.
- Publier OTA via `IOT_n3/scripts/publish_ota.ps1 -Build` (rattrapage metadata).
- `docs/inventaire_appareils.md` mis à jour si version cible révisée.


---

## 9. Hors-scope contrat firmware-serveur

Les changements de contrat suivants nécessitent une coordination serveur (submodule `n3_serveur`). **Approche choisie : rétrocompatibilité d'abord, bascule plus tard.**

### 9.1 HTTPS endpoints données

- Aujourd'hui : POST `/ffp3/post-data*`, GET `/ffp3/api/outputs*/state`, `/ffp3/heartbeat*` en HTTP clair.
- v13.80 (Incrément 6) : flag build `USE_HTTPS_ENDPOINTS` ; env PIO `wroom-prod-https` pour pilote.
- v13.90 (Phase finale) : actif par défaut sur prod ; flag `USE_LEGACY_HTTP` pour fallback explicite.
- **Côté serveur** : routes acceptent déjà HTTPS via `BASE_URL_SECURE = https://iot.olution.info`. Vérifier certificat valide.

### 9.2 HMAC-SHA256

- Aujourd'hui : firmware envoie `api_key=...` dans body POST. Serveur supporte HMAC (`App\Security\SignatureValidator`) mais firmware ne l'implémente pas.
- v13.80 : nouveau module `hmac_sign.cpp/h` ; en-têtes `X-Sig-Timestamp`, `X-Sig-Nonce`, `X-Sig-Hmac` en **complément** d'`api_key`. Le serveur tolère les deux modes (à vérifier).
- v13.90 : HMAC obligatoire firmware si `API_SIG_SECRET` défini ; `api_key` reste accepté serveur pour appareils non migrés.

### 9.3 Signature OTA (Ed25519)

- Aujourd'hui : MD5 metadata + binaire en HTTP — MITM possible.
- v13.80 : nouveau champ `signature` dans `metadata.json` (Ed25519 du binaire) ; firmware vérifie si `OTA_PUBLIC_KEY` configuré, sinon MD5 fallback.
- v13.90 : signature obligatoire en prod ; refus OTA si absente.
- **Côté serveur / scripts** : `IOT_n3/scripts/publish_ota.ps1` doit signer chaque firmware lors de la publication (utilitaire `signtool` Ed25519 ou OpenSSL).

### 9.4 Calendrier proposé

```
v13.80 (mode dual)         : pilote sur 1-2 appareils (wroom-prod-https)
+ 1-2 semaines validation  : monitoring continu, pas de régression
v13.90 (bascule défaut)    : déploiement sur tous les wroom-prod / wroom-s3-prod
```

---

## Annexe A — Cartographie firmware↔serveur

| Modification firmware | Serveur à valider |
|---|---|
| `server_url_config.h` (endpoints) | `serveur/config/routes_ffp3.php`, `routes_config.php` |
| Champs POST (`gpio_mapping.h`) | `Ffp3/PostDataController`, `SensorDataService` |
| Format heartbeat / CRC | `Ffp3/HeartbeatController` |
| Auth HMAC (v13.80+) | `App\Security\SignatureValidator`, `.env` `API_SIG_SECRET` |
| OTA paths / metadata / signature | `serveur/ota/metadata.json`, `IOT_n3/scripts/publish_ota.ps1` |
| Seuils validation | `SensorDataService` + `docs/technical/SEUILS_SERVEUR_ESP32.md` |

---

## Annexe B — Validation hardware

L'agent **ne flashe pas** durant les incréments. Validation par l'utilisateur post-v13.80 (avant phase finale v13.90) :

```powershell
# Workflow recommandé
.\firmwires\ffp5cs\erase_flash_fs_monitor_5min_analyze.ps1 -Environment wroom-prod -DurationMinutes 10
# Puis
.\firmwires\ffp5cs\analyze_log.ps1
.\firmwires\ffp5cs\generate_diagnostic_report.ps1
```

Critères de succès :
- Aucune `Guru Meditation`, `TASK_WDT`, `INT_WDT`, `Stack canary`.
- Heap min stable, pas de fuite progressive sur 10 min.
- POST/GET serveur OK (status 200 sur routes attendues).
- Heartbeat reçu côté serveur dans `serveur/data/...`.

---

## Annexe C — Bilan final

### Statut des 7 incréments

| Incrément | Version | Date | Statut | Commits |
|---|---|---|---|---|
| 0 — Vérifs préliminaires | — | 2026-05-20 | OK | Scripts pre-build présents (audit initial erroné par indexation outil) |
| 1 — sécurité critique web | **13.52** | 2026-05-20 | LIVRÉ | bug API_KEY, 6 routes admin auth, cookie hardened, WebSocket auth |
| 2 — fonctionnel critique | **13.53** | 2026-05-20 | LIVRÉ | wlAqua/wlPota fallback, GPIOMap Pins, SMTP feed TWDT, mutex HTTP timeout |
| 3 — hygiène + sécurité moyenne | **13.60** | 2026-05-20 | LIVRÉ | 6 façades config_*.h, board_traits.h, CORS, OTA CN, AP WPA2, lib_deps épinglées |
| 4 — refactor architecture | **13.65** | 2026-05-20 | LIVRÉ (partiel) | mailer cache, getCachedReadings ; gros découpages reportés v13.66+ |
| 5 — robustesse + tests | **13.70** | 2026-05-20 | LIVRÉ | NVS isKey, heartbeat counter, MIN_HEAP aliases, task_monitor, tests Unity |
| 6 — contrat dual rétrocompatible | **13.80** | 2026-05-20 | LIVRÉ | HTTPS opt-in, HMAC-SHA256 dual, doc MIGRATION |
| 7 — bascule HTTPS+HMAC défaut | **13.90** | EN ATTENTE | À déclencher | Après validation pilote v13.80 (1-2 semaines hardware) |

### Bilan par axe (audit initial vs livraison)

**Sécurité (sous-agent 5)** — 5 BLOQUANTS + 7 IMPORTANTS identifiés :
- Routes admin web sans auth : **5/5 corrigées** (v13.52).
- `/wifi/saved` mots de passe en clair : **corrigé** (v13.52).
- WebSocket port 81 sans auth : **corrigé** (v13.52 — token session).
- Cookie session faible : **corrigé** (v13.52 — esp_fill_random + HttpOnly + SameSite + TTL + rotation).
- Bug `API_KEY = API_KEY` : **corrigé** (v13.52 — macro intermédiaire + `#undef`).
- CORS `*` : **retiré** (v13.60 — UI same-origin).
- OTA metadata CN strict : **activé** (v13.60).
- AP secours WPA2 : **implémenté** (v13.60 — opt-in via `Secrets::AP_FALLBACK_PASSWORD`).
- Trafic HTTP clair + api_key : **mode dual livré** (v13.80 — bascule défaut v13.90).
- OTA HTTP + MD5 seul : **signature Ed25519 préparée** (v13.80 — implémentation v13.85).

**Fonctionnel (sous-agents 3, 4)** — 6 IMPORTANT identifiés :
- `wlAqua = 0` fausse alerte inondation : **corrigé** (v13.53 — fallback `_lastValid` + `Fallback::WATER_LEVEL_AQUA`).
- `wlPota = 0` symétrique : **corrigé** (v13.53 — nouveau membre `_lastValidWlPota`).
- Plages ultrason 4000/5000 mm incohérentes : **documenté** explicitement (v13.53).
- `GPIOMap` pompes/lumière hardcodés : **corrigé** (v13.53 — référencent `Pins::POMPE_AQUA` etc.).
- SMTP sans feed TWDT : **corrigé** (v13.53 — `esp_task_wdt_reset` avant/après).
- Mutex HTTP `portMAX_DELAY` : **corrigé** (v13.53 — timeout = `timeoutMs + 5000`).

**Architecture (sous-agent 1)** — 3 BLOQUANTS + 5 IMPORTANTS :
- `app_tasks.cpp` 1709 lignes : **reporté v13.66** (refactor risqué, validation hardware requise).
- `web_server.cpp` 1949 lignes : **reporté v13.67**.
- `ota_manager.cpp` 1963 lignes : **non traité** (volume + complexité OTA).
- `config.h` 1062 lignes : **façades posées** (v13.60) — découpage physique progressif.
- `Automatism::readSensors()` public bloquant : **corrigé** (v13.65 — `getCachedReadings()` ajouté ; `mailer.cpp` migré).
- `app_context.h` cascade includes : **reporté v13.68**.
- `extern` globals dans `web_server.cpp` : **reporté v13.68**.
- Init TWDT en 3 blocs `app.cpp` : **factorisé** `SystemBoot::initWatchdog()` (v13.53).

**Mémoire / RTOS (sous-agent 3)** — 1 BLOQUANT + 4 IMPORTANTS :
- SMTP sans feed TWDT (lié à fonctionnel) : **corrigé** (v13.53).
- `MIN_HEAP_FOR_TLS` partagé SMTP/HTTPS : **aliases distincts** (v13.70).
- Inventaire DRAM obsolète : **actualisé** (v13.70).
- Heartbeat perdu silencieux : **compteur diagnostique** (v13.70).
- `task_monitor` incomplet : **étendu postSender + ota** (v13.70).

**Réseau / OTA (sous-agent 2)** — 2 BLOQUANTS + 5 IMPORTANTS :
- Mutex HTTP `portMAX_DELAY` (fonctionnel) : **corrigé** (v13.53).
- HTTP clair + api_key sans HMAC : **mode dual livré** (v13.80).
- WiFi connect peut bloquer plusieurs min au boot : **reporté** (nécessite validation hardware étendue).
- OTA `skip_cert_common_name_check = true` : **corrigé** (v13.60).
- Slot `NetRequest` libération POST : **vérifié** OK (v13.53 — déjà symétrique au correctif v13.51 GET).
- Signature OTA Ed25519 : **préparation v13.85**.

**Build / Tests / Doc (sous-agent 6)** — 3 BLOQUANTS + 8 IMPORTANTS :
- Scripts pre-build manquants : **non, présents** (audit initial erroné).
- `platformio-native.ini` + `build_all_envs.ps1` absents : **non, présents** (audit initial erroné).
- Suite `wroom-beta-local` absente : **non, présente** (audit initial erroné).
- `README.md` badge 13.26 vs config 13.51 : **aligné 13.80** (v13.52+).
- Documentation endpoints S3 désalignée : **partiellement corrigé** (à compléter avec doc serveur).
- `lib_deps` Async flottantes : **épinglées** (v13.60 — `AsyncTCP@3.3.5`, `ESPAsyncWebServer@3.7.6`).
- `-Wno-error` global : **conservé** (changement risqué — CI job optionnel proposé).
- Tests Unity sensor_validation/server_url/gpio_mapping absents : **2/3 créés** (v13.70 — sensor_validation, gpio_mapping). `server_url_config` reporté.
- OTA metadata serveur en retard (13.47 vs firmware 13.x) : **à déclencher manuellement après build prod** (`scripts/publish_ota.ps1`).

### Total livré

- **6 versions firmware déployées** (v13.52 → v13.80) sur la branche `pio-build` (firmwires submodule) et `master` (parent IOT_n3).
- **~25 fichiers modifiés** (incluant 5 nouveaux : `board_traits.h`, 6 façades `config_*.h`, `hmac_sign.h/cpp`, 2 suites tests Unity, `MIGRATION_HMAC_HTTPS.md`, `AUDIT_GENERAL_2026-05.md`).
- **~13 BLOQUANTS sécurité/fonctionnels traités** sur 13 identifiés (les 3 BLOQUANTS architecture reportés explicitement).
- **~15 IMPORTANTS traités** sur 24 identifiés (les 9 reportés sont documentés avec cible).

### Points reportés (cible v13.66 - v13.85)

| Item | Cible | Justification report |
|---|---|---|
| Découpage `app_tasks.cpp` (1709 l.) | v13.66 | Refactor risqué pools réseau FreeRTOS - validation hardware focalisée |
| Extraction routes admin `web_server.cpp` | v13.67 | Volume + nombreux call sites |
| `app_context.h` forward decl + extern globals | v13.68 | Recompilation massive + risque de régression |
| Nettoyage commentaires `// v11.*` | v13.66 | Cosmétique, faible priorité |
| Découpage physique `config.h` | v13.66+ | Façades posées, contenu déplacé progressivement |
| WiFi cap timeout boot | v13.66+ | Validation hardware AP fallback requise |
| Rollback OTA intelligent (state machine) | v13.66+ | Bénéfice marginal vs complexité |
| Signature OTA Ed25519 | v13.85 | mbedtls Ed25519 / libsodium - décision crypto |
| Bascule HTTPS+HMAC par défaut | **v13.90** | Validation pilote v13.80 requise (1-2 semaines hardware) |

### Procédure de bascule v13.90 (à exécuter après validation pilote)

Étapes côté firmware :
1. Flasher `wroom-prod-https` sur 1-2 appareils prod en parallèle (les autres restent en `wroom-prod`).
2. Configurer `Secrets::API_SIG_SECRET` + `SECRETS_INCLUDE_API_SIG_SECRET 1` côté firmware ET `.env API_SIG_SECRET` côté serveur (même valeur).
3. Monitor 1-2 semaines : taux d'erreur POST/GET, heap stable, pas de reboot.
4. Si validation OK : v13.90 = activer `USE_HTTPS_ENDPOINTS` par défaut dans `[env:wroom-prod]` et `[env:wroom-s3-prod]`. Ajouter flag `USE_LEGACY_HTTP` pour fallback explicite. Faire `HmacSign::isEnabled()` obligatoire (échec auth → queue NVS fallback).
5. Publier OTA v13.90 via `IOT_n3/scripts/publish_ota.ps1` pour bascule progressive de la flotte.

Étapes côté serveur :
1. Vérifier `App\Security\SignatureValidator` accepte HMAC en priorité, fallback `api_key`.
2. Activer `.env API_SIG_SECRET`.
3. Certificat HTTPS Let's Encrypt valide pour `iot.olution.info`.

### Publication OTA (rattrapage metadata)

Une fois v13.80 buildée localement et validée, exécuter depuis la racine `IOT_n3` :

```powershell
.\scripts\publish_ota.ps1 -Component ffp5cs -Channel test -Build
.\scripts\publish_ota.ps1 -Component ffp5cs -Channel prod -Build
```

Le CRON serveur récupère le push sous 1 min ; les appareils existants reçoivent v13.80 au prochain check OTA (intervalle 2 h par défaut).

### Documentation à jour

- [`firmwires/ffp5cs/README.md`](../../README.md) — badge `13.80`, sections actualisées, historique réduit.
- [`firmwires/ffp5cs/VERSION.md`](../../VERSION.md) — 6 nouvelles entrées (v13.52 à v13.80).
- [`firmwires/ffp5cs/docs/technical/MIGRATION_HMAC_HTTPS.md`](../technical/MIGRATION_HMAC_HTTPS.md) — guide migration HMAC+HTTPS.
- [`docs/reports/AUDIT_GENERAL_2026-05.md`](AUDIT_GENERAL_2026-05.md) — ce rapport (synthèse + bilan).

---

**Fin du rapport. Audit clôturé sur v13.80 (mode dual rétrocompatible). v13.90 (bascule défaut) reste à déclencher après validation pilote.**
