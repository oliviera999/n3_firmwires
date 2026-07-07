# Version uploadphotosserver (ESP32-CAM unifié)

Version actuelle : **2.61** (définie dans `include/config.h`).

---

## Historique

| Version | Date | Modifications |
|---------|------|---------------|
| 2.61 | 2026-07-07 | **Phase 0 arbitrage mails — alerte « OTA échec » non perdue** (cf. `n3_serveur/docs/ARCHITECTURE_MAILS_ARBITRAGE.md`). Si l'envoi SMTP du mail « OTA terminée (échec) » échoue dans `otaMailEndCallback`, l'alerte est mémorisée en RTC (`pendingOtaFailMail` + corps) et retentée aux réveils suivants quand le WiFi est disponible (budget borné à 5 essais, abandon loggué) — avant, un échec d'envoi perdait définitivement l'alerte au deep sleep. Aucun changement d'arbitrage. |
| 2.60 | 2026-07-06 | **Test OTA** : publication distante `msp1` + `ffp3` (validation pipeline cam OTA). Inclut correctifs WiFi v2.58–2.59 (`n3_wifi` 1.2.5). |
| 2.59 | 2026-07-06 | **WiFi** : `disableFastReconnect` sur CAM ; `WIFI_PRE_SCAN_DELAY_MS` 500 ms ; lib `n3_wifi` 1.2.5 — repli **scan synchrone** si scan async échoue (`WIFI_SCAN_FAILED`). |
| 2.58 | 2026-07-06 | **WiFi** : reset radio (`disconnect`, `scanDelete`, `WIFI_OFF` → `STA`) avant connexion au réveil — corrige `Echec connexion — scan` (WIFI_SCAN_FAILED) après deep sleep ou reconnexion rapide RTC. |
| 2.57 | 2026-07-05 | Publication OTA prod : déploiement distant caméras `msp1`, `n3pp`, `ffp3` (envs unifiés esp32cam). Build : fallback `API_SIG_SECRET` dans `config.h` si `credentials.h` ancien. |
| 2.56 | 2026-07-05 | **Audit 2026-07-05 (tous findings)**. **A1** : `planned` plafonné au drainable réel (~16/réveil) → plus d'`aborted`/mail d'alerte à chaque réveil pour un gros backlog. **A2** : le firmware annonce le VRAI backlog comme `total` + drapeau `final=1` quand le backlog est vidé (récap serveur une seule fois). **A3** : fuseau POSIX `"<+01>-1"` (UTC+1 Maroc) appliqué inconditionnellement — corrige le décalage 1 h (créneau + horodatages). **A4** : signature HMAC-SHA256 additive (en-têtes `X-Sig-*`) sur upload multipart + sync + version, si `API_SIG_SECRET` défini (rétro-compatible clé API). **A5** : épinglage TLS opt-in (`n3_data_ca_cert.h`) sur l'upload + OTA metadata en HTTPS (rollback `-DOTA_METADATA_HTTP_ROLLBACK`). **A6/A7** : numéro de séquence réservé/committé une seule fois (peek/commit) — plus de numéro fantôme ni de `pending` qui gonfle en upload direct. **A8** : durées warm-up/AEC exposées dans `config.h`. **A9** : config morte retirée (`SERVER_PORT`, `WIFI_RECONNECT_INTERVAL_MS`), `static_assert` sur le cast `WIFI_LIST`, cadence OTA tenant compte du temps éveillé. **M1/B1** : `adjustExposure` (mesure JPEG inopérante + fuite framebuffer) supprimé — AEC matériel OV2640. **M2** : scan backlog `reserve()` + cap 256→64. **M3** : log heap avant handshake TLS. **M4** : temporaires `String` remplacés par buffers pile en chemins chauds. |
| 2.55 | 2026-07-03 | Republication OTA après validation flash USB **ffp3** v2.54 (COM7, MAC `08:3a:f2:6d:4f:e4`, auto-reset DTR/RTS). |
| 2.54 | 2026-07-03 | **Unification envs** : `msp1` / `n3pp` / `ffp3` = seule stack **espressif32@6.13** + **`esp32cam`** (PSRAM, diagnostic `[DIAG]`) + **HTTPS** (`USE_HTTPS_ENDPOINTS` dans `cam-base`). Suppression des envs `*-cam`, `msp1-https` et de l'ancienne stack pioarduino `esp32dev`. Rollback HTTP : retirer le flag dans `platformio.ini`. |
| 2.53 | 2026-07-03 | **Sync SD / rate-limit** : pause 11 s entre uploads backlog (`GALLERY_UPLOAD_RATE_LIMIT_SECONDS=10` serveur) ; retry HTTP 429 ; budget sync 3 min/réveil pour éviter boucles 429 et réveils trop longs. **Doc** : `msp1-https` hérite de `msp1-cam` (PSRAM + TLS) ; validation terrain 2026-07-03 (README, `docs/HTTPS_MIGRATION.md`). |
| 2.52 | 2026-07-03 | Republish OTA test : validation déploiement pipeline cam (msp1/n3pp/ffp3) après correctif stack OTA v2.51. |
| 2.51 | 2026-07-03 | **Correctif OTA ESP32-CAM** : suppression mail SMTP au demarrage OTA (stack overflow loopTask) ; pile loopTask 32 Ko (`CONFIG_ARDUINO_LOOP_STACK_SIZE`) ; mail fin uniquement en cas d'echec OTA. |
| 2.50 | 2026-07-03 | **Qualité** : `JsonDocument` (ArduinoJson 7) dans `camera_remote` ; README enrichi (NTP, `*-cam`, sync nocturne, moniteur série) ; CI matrice `n3pp` + `ffp3`. |
| 2.49 | 2026-07-03 | **Upload SD streaming** : `MultipartFileStream` — drain backlog sans `malloc` JPEG complet (chunks `UPLOAD_CHUNK_SIZE`) ; refactor `camera_uploader` (retry avec stream neuf). |
| 2.48 | 2026-07-03 | **Cycle de vie réveil** : init SD tôt ; sync backlog hors créneau photo (WiFi+SD, sans caméra) ; skip init caméra si ni WiFi ni SD ; helpers `initSdIfEnabled`, `initCameraPipeline`, `runSyncDrainIfNeeded` ; `SERIAL_BOOT_PAUSE_MS=0` (prod). |
| 2.47 | 2026-07-03 | **NTP offline-first** : restauration NVS + `settimeofday` au réveil, sync NTP TZ `Africa/Casablanca`, logs `[TIME]` ; créneau photo fail-closed si horloge non fiable ; `setup()` réordonné (WiFi → remote → NTP → OTA → caméra si créneau) ; deep sleep d'erreur via `runtimeSleepSeconds` (config distante). Lib `n3_time` : helpers partagés + load NVS sur réveil timer. |
| 2.46 | 2026-07-03 | **Renfort init caméra** : cycle PWDN complet + settle XCLK 150 ms ; ping SCCB avec retries (4×, I2C 50 kHz au dernier essai) ; sans PSRAM saut direct CIF/DRAM (plus SVGA/DRAM) ; reset matériel entre tentatives `esp_camera_init` ; échec capture → deep sleep au lieu de `ESP.restart()`. |
| 2.45 | 2026-06-29 | **Diagnostic SCCB pré-init** : sonde I2C GPIO26/27 avant `esp_camera_init` (ping 0x30, lecture PID OV2640 0x2642, scan bus, XCLK/PWDN) pour distinguer panne nappe/alim d'un echec driver. |
| 2.44 | 2026-06-29 | **Mail premier démarrage reporté** : envoi SMTP après la 1re capture réussie (framebuffer libéré), plus avant OTA/capture — réduit le pic heap sur modules sans PSRAM. |
| 2.43 | 2026-06-29 | **FFP3 URLs canoniques** : chemins `/ffp3gallery/...` sans prefixe `/ffp3/` (GET `outputs_state` recevait HTTP 301 Apache sur `/ffp3/*` alors que le POST version passait en rewrite interne). |
| 2.42 | 2026-06-29 | **PSRAM / init caméra** : `fb_location` explicite (PSRAM ou DRAM, aligné exemple Arduino) ; cascade SXGA→CIF→SVGA/DRAM→QQVGA ; diagnostics `[DIAG]` enrichis (`CONFIG_SPIRAM`, `esp_psram_get_size`) pour distinguer build vs matériel ; échec caméra → deep sleep au lieu de `ESP.restart()` (fini la boucle reboot ~13 s). |
| 2.41 | 2026-06-27 | **Horodatage de capture + classement N-first** : le nom SD devient `/<N sur 10>_<Y-m-d_H-i-s>.jpg` (N en tête → tri robuste même si l'heure est fausse ; `0` si horloge inconnue). Le drain énumère désormais le répertoire SD (parse N+horodatage, rétro-compat `picture<N>.jpg`) et envoie l'heure/compteur de capture au serveur via en-têtes `X-Captured-At` / `X-Capture-Seq` (`camera_uploader`). Horloge entretenue : persistance NVS de l'epoch **à chaque réveil** (la RTC avance pendant le deep sleep ; cold-boot repart d'une heure récente). Helper `cameraSyncBuildSdPath`, `SYNC_MAX_BACKLOG_SCAN`. Contrat serveur n3_serveur ≥ 6.2.0. |
| 2.40 | 2026-06-26 | **Synchronisation hors-ligne du backlog photo** : la carte SD sert de file d'attente locale, les photos non encore reçues par le serveur sont (re)poussées dès le retour du WiFi. Nouveaux modules `camera_uploader` (upload réutilisable mémoire/SD, en-tête `X-Sync-Session`) et `camera_sync` (curseur NVS `up_cursor`, drain hybride incrémental/complet, sessions `sync/start`+`sync/finish`). `sendPhoto()` → `capturePhoto()` (capture + stockage SD, upload direct si pas de SD). Contrat serveur n3_serveur ≥ 6.1.0 (jauge de transfert + mail récap). |
| 2.39 | 2026-05-30 | Refactor modules (`camera_setup`, `camera_upload`, `camera_sleep`, `camera_mail_events`), dépendance `Arduino_JSON`, build fix `getLocalTime` |
| 2.38 | 2026-05-19 | Sécurité/robustesse : vérification OTA `sha256` + signature ECDSA optionnelle, correction bug `WiFi.SSID().c_str()` (pointeur temporaire), deep sleep par défaut rétabli à 600 s, upload HTTP refactorisé (`HTTPClient` + retries), validation serveur `board/sensor`, code HTTP 202 pour photo en corbeille auto |
| 2.36 | 2026-03-31 | Build : `-I` corrigé (`$PROJECT_DIR/..` = racine **firmwires**) pour que `credentials.h` charge bien `firmwires/credentials.h` (SMTP, etc.) ; suppression du doublon `include/credentials.h` qui masquait les macros SMTP |
| 2.35 | 2026-03-31 | Publication OTA test : version > distante ; binaire déployé = env **`msp1-cam`** (même stack que flash USB validé), pas `msp1` (pioarduino) |
| 2.34 | 2026-03-30 | Environnements PlatformIO **`*-cam`** : `platformio/espressif32@6.13.0` + `board = esp32cam` (SPIRAM / `psramFound()` comme stack historique) en parallèle des envs pioarduino |
| 2.33 | 2026-03-30 | Logs `[DIAG]` au boot : puce, flash, RAM interne, tas SPIRAM (total/libre/plus grand bloc), `psramFound()`, seuils SXGA et interprétation |
| 2.32 | 2026-03-30 | Caméra : seuils SPIRAM (libre + plus grand bloc) pour SXGA ; repli automatique CIF/`fb_count` 1 si `esp_camera_init` échoue (évite boucle reset) |
| 2.31 | 2026-03-30 | `SERIAL_BOOT_PAUSE_MS = 4000` (débogage moniteur série au boot) |
| 2.30 | 2026-03-30 | Série : `esp_rom_printf` avant `Serial`, UART0 explicite GPIO3/GPIO1, `setDebugOutput` ; `SERIAL_BOOT_PAUSE_MS` (config) pour débogage moniteur ; rappel deep sleep = silence jusqu’au réveil |
| 2.29 | 2026-03-30 | Build : carte `esp32dev` + `memory_type = dio_qspi` (évite link `esp_psram_*` avec profil `esp32cam` / Arduino 3.3.x) ; détection buffers haute résolution via tas SPIRAM si `psramFound()` est faux |
| 2.28 | 2026-03-30 | Tentative alignement PSRAM (`BOARD_HAS_PSRAM`, flags) — lien cassé sous pioarduino 3.3.x |
| 2.27 | 2026-03-24 | Affichage de la progression OTA en pourcentage (`[OTA][PROGRESS]`) dans le moniteur série via la lib partagée `n3_common` |
| 2.26 | 2026-03-24 | Ajout de logs explicites pour le contrôle distant : URL/HTTP/body du GET `outputs_state` et payload POST version (api key masquée) |
| 2.25 | 2026-03-24 | Catégorisation des logs série (`[SERVER]`, `[CAM]`, `[SD]`, `[WIFI]`, `[CAPTURE]`, `[SLEEP]`) ; mise en avant des échanges HTTP serveur (connexion, POST upload, réponse et body) |
| 2.24 | 2026-03-24 | OTA périodique: remplacement du check OTA à chaque réveil par une vérification toutes les 2h (cumul RTC du deep sleep) |
| 2.22 | 2026-03-23 | Deep sleep temporaire réduit à 15 s + ajout de points de monitoring (boot, WiFi, SD, caméra, NTP, upload HTTP, sleep) |
| 2.10 | 2026-03-12 | Audit échanges firmware-serveur (incrément cohérence) |
| 2.9 | 2026-03-10 | TIME_TO_SLEEP 3→600 s ; HTTP_RESPONSE_TIMEOUT_MS 15 s (dérogation conventions) |
| 2.8 | — | Version précédente |

---

## Références

- Configuration : `include/config.h` → `FIRMWARE_VERSION`
- Inventaire : `docs/inventaire_appareils.md`
