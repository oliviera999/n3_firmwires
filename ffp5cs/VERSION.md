# Historique des versions - FFP5CS

Ce fichier documente l'historique des versions du projet FFP5CS (ESP32 Aquaponie Controller).

## Format de version

Le format de version suit le schéma **MAJOR.MINOR** (ex. 12.15, deux chiffres pour la partie mineure) :
- **MAJOR** : Nouvelles fonctionnalités majeures, refactoring important, changements d'API (incrémenter le premier nombre).
- **MINOR** : Corrections de bugs, optimisations mineures, ajustements (incrémenter les décimales, ex. 12.14 → 12.15).

La version est définie dans `include/config.h` (`ProjectConfig::VERSION`). L’historique est tenu dans ce fichier `VERSION.md`.

---

## Version 13.47 - 2026-04-06

### OTA distant (canal test / wroom-beta)

- **Contexte** : validation de la chaîne OTA avec une version **strictement supérieure** à 13.46 dans `metadata.json` (canal `test` / `esp32-wroom`) et déclenchement « Vérifier OTA » côté serveur (persisté en BDD depuis serveur 5.0.300).

---

## Version 13.46 - 2026-04-06

### NVS — snapshot veille / réveil (pompe aqua et autres actionneurs)

- **Symptôme** : après la première mise en veille légère, la pompe aquarium restait OFF alors qu’elle était ON avant la veille ; `snap_pending` / `snap_aqua` pouvaient ne jamais être écrits en flash.
- **Cause** : dans `NVSManager::saveBool`, l’optimisation « pas de réécriture si inchangé » utilisait `getBool(key, value)` ; pour une **clé absente**, Arduino `Preferences` renvoie la valeur par défaut passée (= la valeur à écrire), donc le code croyait « déjà à jour » et **ne faisait jamais** le premier `putBool`.
- **Correctif** : ne comparer `current == value` que si `isKey(key)` ; sinon forcer l’écriture.
- **Diagnostic** : `saveActuatorSnapshotToNVS` journalise les codes d’erreur NVS si une écriture échoue ; `loadActuatorSnapshotFromNVS` trace explicitement l’absence de `snap_pending`.

---

## Version 13.45 - 2026-04-05

### Boot WROOM — panic cache dans `esp_flash_init`

- **Symptôme** : boucle `Guru Meditation` / `Cache disabled but cached memory region accessed` juste après `entry`, PC dans `memset` appelé depuis `esp_flash_read_chip_id` → `esp_flash_init_main`.
- **Cause** : combinaison d’un **`sdkconfig.defaults`** (export volumineux) **incohérent** sur la fréquence CPU et le SPIRAM, et du **LTO** (`-flto` / `-ffat-lto-objects`) en **wroom-prod**, source connue d’instabilités cache / linkage sur Xtensa.
- **Correctif** : surcharges dans [`sdkconfig_wroom_wdt.txt`](sdkconfig_wroom_wdt.txt) — `CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ=240` aligné sur 240 MHz, désactivation explicite de `ESP32_SPIRAM_SUPPORT`, `SPIRAM_SUPPORT`, `SPIRAM_CACHE_WORKAROUND` ; **suppression du LTO** dans [`platformio.ini`](platformio.ini) (`env:wroom-prod` uniquement).

---

## Version 13.44 - 2026-04-05

### Réseau — veille légère et HTTP

- **`WebClient::fetchRemoteState`** : sérialisation sur le même mutex que `httpRequest` (POST), pour éviter l’usage concurrent de `_http` / `_client` entre `netTask` (GET) et `postSenderTask` (POST).
- **Quiesce avant sommeil** : `AppTasks::quiesceHttpBeforeLightSleep` attend des files `g_netQueue` / `g_postSenderQueue` vides puis acquiert le mutex transport ; appelé depuis `automatism_sleep.cpp` après le mail de veille et avant `goToLightSleep`. `releaseHttpAfterLightSleep` relâche le mutex immédiatement après le retour de `goToLightSleep` (avant fetch / restauration actionneurs).
- **`NetworkConfig::LIGHT_SLEEP_HTTP_QUIESCE_TIMEOUT_MS`** : timeout global de la phase quiesce (WROOM / S3).

---

## Version 13.43 - 2026-04-04

### Tests `wroom-beta-local` — invocation smoke

- **`test_wroom_beta_local_docker_integration.ps1`** : appel direct `& local-smoke-test.ps1` (splat hashtable) au lieu d un second `powershell.exe`, pour un binding correct de `-BaseUrl` / `-AdminToken`.
- **`run_wroom_beta_local_test_suite.ps1`** : etapes lancees via `Start-Process powershell.exe -ArgumentList (-File ...)` (binding correct des parametres ; le splat tableau `& $Script` ne convenait pas). Correction parametre `-ScriptArguments` pour l etape `unit-native` (evitait `-Args` invalide).

---

## Version 13.42 - 2026-04-04

### Dashboard web (LittleFS) — bruit console

- **common.js** : niveau de log par défaut **INFO** (2) au lieu de DEBUG (3).
- **websocket.js** : les traces `[DEBUG]` passent par un garde aligné sur `LogConfig` (affichées seulement si le niveau DEBUG ou TRACE est actif).

---

## Version 13.41 - 2026-04-04

### Documentation tests `wroom-beta-local`

- **ffp5cs/docs/README.md** : exemples batterie quick/full, `-AuthMode both`, fichier `.beta-local-test.env`.
- **INVENTAIRE_SCRIPTS_FFP5CS.md** : ligne unique pour `.beta-local-test.env.example` (suppression doublon).

---

## Version 13.40 - 2026-04-04

### Tests `wroom-beta-local` : auth Docker et orchestration

- **Secrets locaux** : fichier optionnel `scripts/.beta-local-test.env` (non versionne, modele `.beta-local-test.env.example`) pour token admin, API key, URL LAN.
- **Integration Docker** : `test_wroom_beta_local_docker_integration.ps1` — modes `AuthMode` token/session/both, override compose pour variables d’environnement, smoke via `serveur/tools/local-smoke-test.ps1`, option `-RequireHeartbeat` pour le test serie.
- **Batterie de tests** : `run_wroom_beta_local_test_suite.ps1` + `wroom_beta_local_test_scenarios.json`.
- **Gitignore** : exclusion de `ffp5cs/scripts/.beta-local-test.env` (racine et `ffp5cs/.gitignore`).

---

## Version 13.39 - 2026-04-04

### Suite de tests `wroom-beta-local` (options 1, 3, 5)

- **URL locale externalisee** : suppression de l'IP locale versionnee dans `platformio.ini` ; ajout d'un override non versionne `include/local_server_overrides.h` (modele `include/local_server_overrides.h.example`).
- **Config testable en natif** : extraction des regles d'URLs serveur dans `include/server_url_config.h`, reutilisees par `include/config.h` (meme comportement firmware, mieux testable hors carte).
- **Tests unitaires natifs** : nouvelle suite `test_server_url` (validation `post-data-test`, `heartbeat-test`, base OTA locale) integree a `platformio-native.ini` et `scripts/test_unit_all.ps1`.
- **Tests sur cible / integration locale** :
  - `scripts/test_wroom_beta_local_serial.ps1` : upload + capture serie + assertions endpoints/statut HTTP.
  - `scripts/test_wroom_beta_local_docker_integration.ps1` : stack Docker locale + generation override URL LAN + test serie + verification BDD (`ffp3Data2` / `ffp3Heartbeat2`).
- **CI build** : ajout de l'option `-IncludeBetaLocal` dans `scripts/build_all_envs.ps1` et `scripts/run_ci_checks.ps1`.
- **Fichiers** : `platformio.ini`, `platformio-native.ini`, `include/config.h`, `include/server_url_config.h`, `include/local_server_overrides.h.example`, `.gitignore`, `scripts/test_unit_all.ps1`, `scripts/build_all_envs.ps1`, `scripts/run_ci_checks.ps1`, `scripts/test_wroom_beta_local_serial.ps1`, `scripts/test_wroom_beta_local_docker_integration.ps1`, documentation.

---

## Version 13.38 - 2026-04-04

### Profil `wroom-beta-local` pour serveur local

- **Nouveau profil PlatformIO** : ajout de `wroom-beta-local`, clone de `wroom-beta` avec un flag dédié `USE_LOCAL_SERVER_ENDPOINTS`.
- **Endpoints serveur** : en mode local, toutes les URLs distantes passent par `LOCAL_SERVER_BASE_URL` (POST/GET/heartbeat + base OTA).
- **Configuration** : URL locale par défaut dans `platformio.ini` (`LOCAL_SERVER_BASE_URL`), modifiable selon l'IP du serveur LAN.
- **Fichiers** : `platformio.ini`, `include/config.h`, `docs/README.md`, `VERSION.md`.

---

## Version 13.37 - 2026-04-02

### Réveil light sleep : restauration actionneurs avant POST réseau

- **Problème** : après veille, la pompe aquarium (et autres actionneurs restaurés) repassait systématiquement OFF car `sendFullUpdate` s’exécutait **avant** `restoreActuatorsAfterWake`, publiant `etatPompeAqua=0` puis le poll `parseAndApply` réappliquait l’état serveur.
- **Correctif** : appeler `restoreActuatorsAfterWake` **immédiatement** après `goToLightSleep`, avant attente WiFi / fetch / envoi.
- **Fichiers** : `src/automatism/automatism_sleep.cpp`, `include/config.h`, `VERSION.md`.

---

## Version 13.36 - 2026-04-01

### Link wroom-prod : marge dram0 (netTask stack BSS)

- **Problème** : échec linker `region dram0_0_seg overflowed by 1472 bytes` sur `wroom-prod` (PIO + pioarduino 55.03.37, GCC 14).
- **Correctif** : `NET_TASK_STACK_SIZE` porté à **12800** (−1576 × `StackType_t` vs 14376) pour **WROOM + PROFILE_PROD** uniquement — la stack mail dans `TaskConfig` ne réserve pas de BSS (pas de `mailTask` statique). À valider au moniteur (HWM netTask) après déploiement.
- **Fichiers** : `include/config.h`, `VERSION.md`.

---

## Version 13.35 - 2026-03-24

### Rebuild + republication OTA pour validation chaîne mm

- **Version firmware** : incrémentation `13.34` -> `13.35` pour tracer une publication dédiée à la vérification des niveaux ultrasons côté serveur distant.
- **Déploiement OTA** : republication des canaux **`ffp5-wroom-beta`** et **`ffp5-wroom-prod`** avec binaires reconstruits.
- **Objectif** : éliminer le doute de binaire/canal en production avant validation terrain des valeurs `EauAquarium/EauReserve/EauPotager`.
- **Fichiers** : `include/config.h`, `VERSION.md`.

---

## Version 13.34 - 2026-03-24

### Publication OTA WROOM beta + prod

- **Version firmware** : incrémentation de `13.33` vers `13.34` pour préparer une publication OTA cohérente.
- **Déploiement OTA demandé** : publication des canaux **`ffp5-wroom-beta`** et **`ffp5-wroom-prod`** via le script racine `scripts/publish_ota.ps1`.
- **Fichiers** : `include/config.h`, `VERSION.md`.

---

## Version 13.33 - 2026-03-24

### Ultrasons en millimètres (expérimental)

- **Mesure ultrason** : conversion µs -> **mm** dans `UltrasonicManager` (lecture simple, avancée et réactive), avec logs alignés en mm.
- **Validation/fallback** : bornes ultrason et fallbacks API `/json` passés en mm (`wlAqua`, `wlTank`, `wlPota`).
- **Automatismes** : comparaisons avec les seuils serveur conservées en cm côté config mais converties en mm au moment des décisions (pompe/alertes/inondation) pour garder une logique cohérente.
- **Marée / inflexion** : hystérésis et triggers adaptés à l’échelle mm (`+30 mm` au lieu de `+3 cm`).
- **UI locale** : libellés des courbes d’eau mis à jour en mm, valeurs fallback front alignées.
- **Fichiers** : `include/config.h`, `include/sensors.h`, `include/automatism/automatism_sync.h`, `src/sensors.cpp`, `src/system_sensors.cpp`, `src/automatism.cpp`, `src/automatism/automatism_sleep.cpp`, `src/automatism/automatism_sync.cpp`, `data/shared/common.js`, `data/shared/websocket.js`, `VERSION.md`.

---

## Version 13.32 - 2026-03-24

### Reset priorisé OTA (local et distant)

- **Règle appliquée** : sur une demande de reset (`resetMode`), le firmware tente d'abord une vérification OTA ; si une mise à jour est disponible, elle est lancée avant tout reset manuel.
- **Reset local (web embarqué)** : commande `cmd=resetMode` met à jour la réponse (`OTA BEFORE RESET` quand une OTA démarre) et n'appelle `ESP.restart()` immédiatement que si aucune OTA n'est disponible (ou si son démarrage échoue).
- **Reset distant (GPIO 110)** : au front montant, même logique OTA-prioritaire ; `ESP.restart()` direct uniquement en fallback.
- **Fichiers** : `src/web_server.cpp`, `src/gpio_parser.cpp`, `include/config.h`, `VERSION.md`.

---

## Version 13.31 - 2026-03-23

### Test OTA aquaponie-test

- **Objectif** : incrément de version pour valider un cycle OTA sur le canal test WROOM (`channels.test.esp32-wroom` → `esp32-wroom-beta/firmware.bin`, endpoints `post-data-test` / `outputs-test` / `heartbeat-test` via `USE_TEST_ENDPOINTS` sur `wroom-beta`).
- **Déploiement** : `scripts/publish_ota.ps1 -Targets ffp5-wroom-beta -Build`.
- **Fichiers** : `include/config.h`, `VERSION.md`.

---

## Version 13.30 - 2026-03-23

### Correctif link DRAM wroom-beta (build)

- **Symptôme** : échec link `dram0_0_seg overflowed by 1464 bytes` sur `wroom-beta` après restauration du correctif boot.
- **Ajustement minimal** : réduction de stacks **statiques** uniquement en profil `PROFILE_BETA` sur WROOM :
  - `SENSOR_TASK_STACK_SIZE` : `3072 -> 2560`
  - `OTA_TASK_STACK_SIZE` : `12288 -> 11264`
- **Impact** : récupération d'environ 1.5 KB en `.dram0.bss`, sans toucher aux valeurs prod ni test.
- **Fichiers** : `include/config.h`, `VERSION.md`.

---

## Version 13.29 - 2026-03-23

### Correctif boot loop WROOM-beta (Cache error au démarrage)

- **Cause isolée par bisect matériel** : premier commit fautif `5e8f8a7`, dernier commit sain `a3c76af`.
- **Cause technique** : `sdkconfig.defaults` contenait des options incompatibles WROOM (profil généré/élargi), provoquant un panic très tôt (`Cache disabled but cached memory region accessed`).
- **Correctif appliqué** : restauration de `sdkconfig.defaults` depuis l'état sain (`a3c76af`) tout en conservant le reste des changements récents.
- **Validation** : build + flash `wroom-beta` puis monitor série sans panic (boot normal et cycle WiFi actif).
- **Fichiers** : `sdkconfig.defaults`, `include/config.h`, `VERSION.md`.

---

## Version 13.28 - 2026-03-23

### Correctif boot loop WROOM-beta apres P1

- **Rollback ciblé stacks** : retour au dimensionnement précédent des stacks statiques dans `app_tasks.cpp` pour `sensorTask`, `automationTask` et `netTask`, afin d'éliminer le facteur de risque introduit par la normalisation.
- **OTA inchangé** : `otaTask` conserve le comportement historique (`/ sizeof(StackType_t)`).
- **netRPC conservé** : le correctif de libération de slot sur timeout/notification tardive est maintenu.
- **Fichiers** : `src/app_tasks.cpp`, `include/config.h`, `VERSION.md`.

---

## Version 13.27 - 2026-03-23

### Audit WROOM P1/P2 : netRPC, stacks FreeRTOS et cohérence partitions/doc

- **netRPC timeout/pool** : dans `netRpcAlloc`, quand le caller timeout puis reçoit une notification tardive de fin, le slot `NetRequest` est libéré immédiatement pour éviter un slot bloqué jusqu'au cycle suivant.
- **Stacks FreeRTOS** : unification de l'unité de profondeur de pile via `STACK_DEPTH_WORDS(...)` pour toutes les tâches statiques (`sensorTask`, `automationTask`, `netTask`, `otaTask`) afin d'éliminer l'ambiguïté bytes/mots.
- **Cohérence config/docs WROOM** : commentaires de `platformio.ini` (env `wroom-beta`) réalignés avec l'héritage réel (`wroom-prod` + partition `ota_fs_mail`), et mise à jour de `FLASH_ADDRESSES_WROOM_PROD.txt` vers la table effectivement utilisée.
- **Fichiers** : `src/app_tasks.cpp`, `platformio.ini`, `FLASH_ADDRESSES_WROOM_PROD.txt`, `include/config.h`, `VERSION.md`.

---

## Version 13.26 - 2026-03-23

### Audit mémoire wroom-beta : bornage WiFi et suppression `String` au scan

- **Bornage sécurité** : dans `WifiManager::connect()`, les tableaux locaux (`cand`, `order`) sont maintenant bornés à `NVSConfig::MAX_WIFI_SAVED_NETWORKS` avec log explicite si `WIFI_COUNT` dépasse la capacité locale. Évite tout dépassement de tableau sur stack.
- **Anti-fragmentation** : suppression des `String` temporaires dans les scans WiFi (`WiFi.SSID(j)`) au profit de l'API ESP-IDF (`esp_wifi_scan_start` + `esp_wifi_scan_get_ap_records`) avec buffers `char[]`.
- **Helper AP SSID** : `WiFiHelpers::getAPSSID()` n'utilise plus `WiFi.softAPSSID()` (String), lecture directe via `esp_wifi_get_config(WIFI_IF_AP, ...)`.
- **Vérification point 2** : `xTaskCreatePinnedToCore` / `xTaskCreateStaticPinnedToCore` validés sur la toolchain actuelle ; `StackType_t` est `uint8_t`, la profondeur est traitée en bytes, donc pas de bug de conversion détecté dans le code actuel.
- **Fichiers** : `src/wifi_manager.cpp`, `include/wifi_manager.h`, `include/config.h`, `VERSION.md`.

---

## Version 13.25 - 2026-03-22

### Test OTA distant (wroom-beta)

- **Objectif** : incrément de version pour valider un cycle OTA distant complet sur le canal beta (`ffp5-wroom-beta`).
- **Déploiement OTA** : publication du firmware beta via `scripts/publish_ota.ps1` (cible `ffp5-wroom-beta`).
- **Fichiers** : `include/config.h`, `VERSION.md`.

---

## Version 13.24 - 2026-03-21

### OTA HTTP aussi pour S3 (alignement WROOM/S3)

- **OTA S3 en HTTP** : la récupération `metadata.json` utilise maintenant HTTP côté S3, comme WROOM, pour homogénéiser les mises à jour OTA et réduire les échecs liés à TLS.
- **Downgrade URL OTA** : conversion HTTPS → HTTP appliquée aussi aux URLs firmware/filesystem OTA sur S3 (comme sur WROOM).
- **Intégrité** : l’intégrité du binaire reste contrôlée par la vérification MD5 avant flash.
- **Fichiers** : `include/ota_config.h`, `src/ota_manager.cpp`, `include/config.h`, `VERSION.md`.

---

## Version 13.23 - 2026-03-21

### Build Windows : redirection C:\pio-builds, scripts et nettoyage

- **pio_redirect_build_dir.py** : sous Windows, artefacts de build par défaut dans `C:\pio-builds\<slug-projet>\<env>\` (désactivation : `N3_PIO_BUILD_REDIRECT=0`, racine : `N3_PIO_BUILD_ROOT`).
- **Get-PioBuildHelpers.ps1** : chemins firmware / littlefs / ELF alignés pour `publish_ota.ps1`, `build_production.ps1`, `decode_backtrace`, `clean_s3_build`, etc.
- **clean-firmware-builds.ps1** (racine IOT_n3) : nettoyage `.pio` par projet + options `-IncludePioBuildsRoot`, `-IncludeLegacyFfp5Mirror` (ancien `C:\ffp5cs_build`).
- **build_production.ps1** : environnement explicite `wroom-prod` (param `-Environment`), plus de chemin obsolète `esp32dev`.
- **Compatibilité redirection** : `pio_add_mklittlefs_path.py` (`ensure_build_subdirs`) et `s3_patch_ldgen_fragments_file.py` utilisent `BUILD_DIR` au lieu d’un chemin fixe `.pio/build` (évite `FRAMEWORK_DIR None` / incohérences avec `pio_redirect_build_dir.py`).
- **Fichiers** : `platformio.ini`, `test psram s3*/platformio.ini`, `scripts/publish_ota.ps1`, `scripts/find-bugs.ps1`, `run_s3_build_from_safe_path.bat`, `tools/coredump/analyze_coredump.py`, `pio_add_mklittlefs_path.py`, `s3_patch_ldgen_fragments_file.py`, `VERSION.md`, `config.h`.

---

## Version 13.22 - 2026-03-20

### OTA WROOM HTTP fallback, boot settle, scripts build multi-env

- **OTA WROOM HTTP** : metadata OTA en HTTP sur WROOM (TLS impossible, heap fragmenté ~31 KB contigus vs ~45 KB requis par mbedTLS). S3 reste en HTTPS. Intégrité binaire garantie par vérification MD5.
- **Boot settle** : attente progressive au boot (8s max) jusqu'à obtention d'un bloc contigu >= 45 KB. Libération temporaire de la réserve mail pour maximiser le bloc disponible.
- **OTA périodique** : seuil de bloc contigu minimal (28 KB WROOM, TLS complet S3) avant vérification.
- **platformio.ini** : moniteur série activé en wroom-beta (ENABLE_SERIAL_MONITOR=1).
- **build_all_envs.ps1** : réécriture complète — paramètres (-Envs, -Clean, -StopOnError, -Verbose), nettoyage auto au basculement WROOM/S3, rapport résumé en fin d'exécution.
- **run_ci_checks.ps1** : traduction FR, option -AllEnvs pour build multi-env via build_all_envs.ps1.
- **Documentation** : matrice des environnements dans README.md, descriptions mises à jour dans INVENTAIRE_SCRIPTS_FFP5CS.md.
- **Fichiers** : `config.h`, `ota_config.h`, `app_tasks.cpp`, `ota_manager.cpp`, `platformio.ini`, `build_all_envs.ps1`, `run_ci_checks.ps1`, `docs/README.md`, `docs/INVENTAIRE_SCRIPTS_FFP5CS.md`.

---

## Version 13.18 - 2026-03-19

### Incrément version — déploiement OTA (wroom-beta)

- **Objectif** : version 13.18 pour publication OTA canal beta.
- **Fichiers** : `include/config.h`, `VERSION.md`.

---

## Version 13.17 - 2026-03-18

### Incrément version — déploiement OTA (wroom-beta, wroom-prod)

- **Objectif** : version 13.17 pour publication OTA (canaux prod et test).
- **Fichiers** : `include/config.h`, `VERSION.md`.

---

## Version 13.16 - 2026-03-18

### Correctif TWDT au boot (WROOM / IDF 5) : éviter « TWDT was never initialized »

- **Problème** : au boot, erreur « E (13) task_wdt: esp_task_wdt_deinit(644): TWDT was never initialized » lorsque le framework n’a pas encore initialisé le TWDT ; appel à `esp_task_wdt_deinit()` avant tout `esp_task_wdt_init()` provoquait ce log et pouvait affecter la suite du boot.
- **Correctif** : pour WROOM avec IDF 5+, appeler `esp_task_wdt_init(&cfg)` en premier ; si retour `ESP_ERR_INVALID_STATE` (TWDT déjà configuré), alors `esp_task_wdt_deinit()` puis `esp_task_wdt_init(&cfg)`. Ainsi on ne fait `deinit` que lorsque le TWDT était déjà initialisé (reconfig).
- **Fichiers** : `src/app.cpp` (deux blocs IDF 5 WROOM : avant Serial et reconfig 30s/60s), `include/config.h` (VERSION 13.16), `VERSION.md`. Correctif lié au bon déroulement du boot et de l’OTA (pas de blocage early boot).

---

## Version 13.15 - 2026-03-18

### Incrément version — déploiement OTA wroom-beta

- **Objectif** : version 13.15 pour publication OTA (canal test, esp32-wroom-beta).
- **Fichiers** : `include/config.h`, `VERSION.md`.

---

## Version 13.14 - 2026-03-18

### Correctif TWDT : nourrissage watchdog otaTask et postSender

- **Problème** : redémarrages en boucle (COM3) — « Task watchdog got triggered … otaTask » (parfois postSender). otaTask bloquait 2h dans `xQueueReceive` sans appeler `esp_task_wdt_reset()` ; postSender bloquait en `portMAX_DELAY` sans limite.
- **Correctif otaTask** : attente par pas de **OTA_WDT_FEED_INTERVAL_MS** (10s) au lieu de 2h d’un coup ; `esp_task_wdt_reset()` à chaque réveil (timeout ou message) ; périodicité 2h conservée via `lastOtaCheckMs` / `OTA_CHECK_INTERVAL_MS`.
- **Correctif postSender** : `xQueueReceive` avec timeout **15s** au lieu de `portMAX_DELAY` ; `esp_task_wdt_reset()` à chaque retour.
- **Fichiers** : `include/config.h` (VERSION 13.14, `OTA_WDT_FEED_INTERVAL_MS`), `src/app_tasks.cpp`, `VERSION.md`.

---

## Version 13.13 - 2026-03-18

### Incrément version — déploiement OTA

- **Objectif** : version 13.13 pour publication OTA (serveur/ota/, URLs /ota/).
- **Fichiers** : `include/config.h`, `VERSION.md`.
- **Publication wroom-beta** : même binaire sans LittleFS que wroom-prod ; script racine `publish_ota.ps1` avec `IncludeFsFfp5 = false` pour `ffp5-wroom-beta`. Doc : `docs/technical/OTA_PUBLISH.md`.

---

## Version 13.12 - 2026-03-18

### OTA unifiée sur /ota/

- **Changement** : `OTA_BASE_PATH = "/ota/"` ; metadata et binaires publiés via `scripts/publish_ota.ps1` (racine IOT_n3) dans **serveur/ota/** (plus de dépôt ffp3/ota).
- **Script** : `firmwires/ffp5cs/scripts/publish_ota.ps1` délègue au script racine (cibles ffp5-wroom-prod, ffp5-wroom-beta, ffp5-s3-prod, ffp5-s3-test).
- **Fichiers** : `include/config.h`, `docs/technical/OTA_PUBLISH.md`, `docs/INVENTAIRE_SCRIPTS_FFP5CS.md`.

---

## Version 13.11 - 2026-03-18

### OTA priorité absolue pendant l’exécution

- **Objectif** : pendant `checkForUpdate()` et `performUpdate()`, otaTask a la priorité absolue (plus préemptée par netTask/postSender) pour réduire TWDT et risques de stack canary.
- **Changement** : montée en priorité à `OTA_TASK_PRIORITY_WHILE_RUNNING` (10) avant la vérification OTA, restauration à `OTA_TASK_PRIORITY` (3) à la fin (boot et boucle périodique).
- **Fichiers** : `include/config.h`, `src/app_tasks.cpp`.

---

## Version 13.10 - 2026-03-18

### WROOM boot-loop (postSender + otaTask TWDT)

- **Problème** : boucle reboot — stack canary **postSender** (pile 4 Ko insuffisante pour HTTPS heartbeat) ; **TWDT otaTask** après **waitForNetworkReady** (DNS `hostByName` lent > 30 s sans nourrir le TWDT).
- **Correctifs** : `POST_SENDER_TASK_STACK_SIZE` WROOM **8192** (aligné S3) ; `esp_task_wdt_reset()` dans `PowerManager::waitForNetworkReady()` (boucle + avant/après stabilisation).
- **Flash complet** : `pio run -e wroom-prod -t upload --upload-port COMx` (bootloader + partitions + firmware) ; exécuter avec l’ESP branché et le port série libre.
- **Option OTA boot différé (après netTask)** : non retenue — les correctifs ci-dessus couvrent le scénario observé ; réouvrir si DNS/TWDT persistent malgré 13.10.
- **Fichiers** : `include/config.h`, `src/power.cpp`.

---

## Version 13.09 - 2026-03-18

### Link dram0 WROOM + partitions S8 + build matriciel

- **Contexte** : `wroom-prod` / `wroom-beta` échouaient au link (`dram0_0_seg` ~2 Ko) ; `wroom-test` ~10 Ko de plus (AsyncWeb/WebSockets) ; `wroom-s3-test-devkit` partitions CSV chevauchées ; tests natifs Unity cassés par héritage `framework=arduino` de `[env]`.
- **Changements** :
  - **WROOM prod/beta** : `NET_TASK_STACK_SIZE` = 14384 (≈−2 Ko BSS vs 16384).
  - **WROOM `PROFILE_TEST` (wroom-test)** : stacks réduites (automation 8K, OTA 9K, net 9216, mail 12K) pour le link ; surveiller HWM sur carte.
  - **Partitions 8 Mo S3** : `app0` @ `0x20000` (alignement 64 Ko), apps `0x2F0000`, LittleFS `0x200000`.
  - **Tests natifs** : `platformio-native.ini` + `pio test -c platformio-native.ini -e native` ; `scripts/test_unit_all.ps1` mis à jour.
  - **Script** : `scripts/build_all_envs.ps1` (build séquentiel de tous les envs firmware + compilation tests natifs).
- **Fichiers** : `include/config.h`, `config/partitions/partitions_esp32_s3_8mb.csv`, `platformio.ini`, `platformio-native.ini`, `scripts/build_all_envs.ps1`, `scripts/test_unit_all.ps1`.

---

## Version 13.08 - 2026-03-17

### Test OTA / déploiement serveur

- **Objectif** : Incrément pour validation OTA distante (metadata + firmware sur iot.olution.info).
- **Fichiers** : `include/config.h` (version 13.08).

---

## Version 13.07 - 2026-03-17

### OTA prioritaire — tâche dédiée

- **Objectif** : Rendre l’OTA prioritaire sur tout (config, POST, heartbeat) et éviter les stack overflows dans netTask lors de checkForUpdate/performUpdate.
- **Changements** :
  - Nouvelle tâche FreeRTOS **otaTask** (priorité 3, stack 12 KB) : exécute uniquement la vérification et l’application OTA au boot puis en boucle (périodique 2h ou sur trigger). Créée avant netTask.
  - **netTask** ne fait plus aucun appel à OTAManager (boot OTA, retry heap, périodique 2h et cas OtaCheck retirés ou délégués à otaTask).
  - Déclenchement OTA (triggerOtaCheck, bouton web « Vérifier OTA ») envoie un message à la file `g_otaTriggerQueue` ; l’endpoint POST /api/ota notifie la tâche OTA au lieu d’appeler directement checkForUpdate/performUpdate.
- **Fichiers** : `include/config.h` (OTA_TASK_PRIORITY, OTA_TASK_STACK_SIZE, version 13.07), `src/app_tasks.cpp` (otaTask, file trigger, retrait OTA de netTask), `src/web_server.cpp` (api/ota), `src/system_boot.cpp`, `include/app_tasks.h`.

---

## Version 13.06 - 2026-03-17

### Déploiement OTA unifié

- **Objectif** : Incrément pour déploiement OTA (n3pp, msp, ffp5cs) via script unifié.
- **Fichiers** : `include/config.h` (version 13.06).

---

## Version 13.05 - 2026-03-17

### Test OTA

- **Objectif** : Incrément de version pour valider le déploiement OTA (wroom-beta).
- **Fichiers** : `include/config.h` (version 13.05).

---

## Version 13.04 - 2026-03-16

### Correctif logs NVS sur S3 PSRAM (boot)

- **Contexte** : En env `wroom-s3-test-psram`, Serial n'est pas démarré au boot ; les logs NVS (Serial.println) ne s'affichaient pas ou provoquaient des accès invalides.
- **Correctif** : Dans `nvs_manager.cpp`, pour `BOARD_S3` et `BOARD_HAS_PSRAM`, utilisation de `BOOT_LOG` à la place de `Serial` pour tous les messages NVS (init, mutex, stats, migration).
- **Fichiers** : `ffp5cs/src/nvs_manager.cpp`, `include/config.h` (version 13.04).

---

## Version 13.03 - 2026-03-16

### Correctif stack overflow postSender (S3)

- **Problème** : En wroom-s3-test, crash en boucle — `Stack canary watchpoint triggered (postSender)` (Guru Meditation). GET outputs/state OK, POST/heartbeat jamais envoyés.
- **Cause** : Tâche postSender (fire-and-forget POST/heartbeat) avec stack 4 KB ; chemin HTTP/postToUrl sur S3 dépasse la pile.
- **Correctif** : Pour `BOARD_S3`, `POST_SENDER_TASK_STACK_SIZE` = 8192 (8 KB). WROOM reste 4 KB.
- **Fichiers** : `include/config.h` (TaskConfig, version 13.03).

---

## Version 13.02 - 2026-03-16

### PSRAM S3 – documentation et choix d’env

- **Documentation** : Section « Choix d’environnement » dans `docs/technical/ESP32S3_HARDWARE_REFERENCE.md` (WiFi requis → wroom-s3-test/prod ; PSRAM requise, WiFi optionnelle → wroom-s3-test-psram). Section « Évolution – PSRAM + WiFi (axe 2) » : vérification releases plateformes, issue #8377 (Wontfix), actions si correctif disponible.
- **Config** : Commentaire dans `include/config.h` pour `POST_PAYLOAD_MAX_SIZE` S3 avec PSRAM (4096, heap étendu 8 Mo).
- **Fichiers** : `docs/technical/ESP32S3_HARDWARE_REFERENCE.md`, `include/config.h` (version 13.02).

---

## Version 13.01 - 2026-03-16

### Correctif réseau S3 sans PSRAM (wroom-s3-test)

- **Problème** : Au boot, échec création `g_postSenderQueue` (heap insuffisant) → netTask et tâches réseau jamais créées → aucun POST/GET vers le serveur.
- **Cause** : Sur S3 sans PSRAM, `POST_PAYLOAD_MAX_SIZE` était 4096 ; la queue (4 × ~4 Ko) nécessitait ~16 Ko contigus après réserve Mail 32 Ko et serveur web.
- **Correctif** : Pour `BOARD_S3` sans `BOARD_HAS_PSRAM`, `POST_PAYLOAD_MAX_SIZE` = 896 (comme WROOM). Payload sync typ. < 800 bytes.
- **Fichiers** : `include/config.h` (BufferConfig, version 13.01).

---

## Version 13.00 - 2026-03-16

### Version majeure (release)

- **Changement** : Passage à la version majeure 13.00. Commit de release : merge de la branche pio-build dans master.
- **Documentation** : VERSION.md et config.h alignés.

---

## Version 12.47 - 2026-03-15

### Fire-and-forget POST (post-data + heartbeat)

- **Changement** : Tâche dédiée `postSenderTask` pour l’envoi HTTP des POST (post-data et heartbeat). netTask transmet le payload à la queue dédiée et libère le slot immédiatement ; plus de blocage 6–18 s par POST.
- **Heartbeat** : Envoi fire-and-forget côté appelant (plus d’attente dans `netRpcAlloc`, plus de slot netRequest réservé). Payload construit via `WebClient::buildHeartbeatPayload`, puis message envoyé à `postSenderQueue`.
- **Post-data** : En cas de file postSender pleine, fallback `queueFailedPost` (NVS) pour conserver l’offline-first.
- **Mutex HTTP** : Sérialisation des appels HTTP (GET dans netTask, POST dans postSenderTask) via un mutex dans `WebClient::httpRequest`.
- **Fichiers** : `src/app_tasks.cpp` (PostSenderMsg, postSenderTask, création queue/tâche dans start, netTask PostRaw/Heartbeat), `src/web_client.cpp` (mutex, buildHeartbeatPayload, postToUrl), `include/web_client.h`, `include/config.h` (TaskConfig::POST_SENDER_*).

---

## Version 12.46 - 2026-03-15

### Correction crash au boot (LoadProhibited 0x54)

- **Changement** : `AppTasks::start()` appelé avant `postConfiguration()` dans `setup()` ; garde `if (!g_netQueue) return false;` au début de `netPostRaw()`.
- **Raison** : `postConfiguration()` envoie un POST `resetMode=0` via `sendFullUpdate` → `netPostRaw` → `xQueueSend(g_netQueue, ...)` alors que `g_netQueue` n’était pas encore créé (il l’est dans `AppTasks::start()`), d’où crash.
- **Fichiers** : `src/app.cpp`, `src/app_tasks.cpp`.

---

## Version 12.45 - 2026-03-15

### Partition wroom-prod / wroom-beta sans FS (pas de serveur embarqué)

- **Changement** : wroom-prod et wroom-beta utilisent `partitions_esp32_wroom_ota_no_fs.csv` (OTA dual, pas de partition spiffs). App 0x1F8000 par slot (~2 Mo).
- **Raison** : Pas de serveur web embarqué sur ces envs ; éviter montage FS inutile et crash (LoadProhibited) au boot. Plus d’espace pour l’app.
- **Fichiers** : `config/partitions/partitions_esp32_wroom_ota_no_fs.csv`, `platformio.ini`, `include/ota_config.h` (MAX_FILESYSTEM_SIZE=0, OTA_APP_PARTITION_SIZE=0x1F8000 si DISABLE_ASYNC_WEBSERVER), `scripts/publish_ota.ps1`, `docs/technical/OTA_PUBLISH.md`.

---

## Version 12.44 - 2026-03-15

### Core dump désactivé partout (audit système)

- **Changement** : `FFP5CS_COREDUMP_ENABLE=0` pour tous les env ; partitions sans coredump pour wroom-test et wroom-s3-test.
- **Raison** : Évite boot bloquant ("No core dump partition found!"), source de bugs. Les logs série, NVS (capturePanicInfo) et monitor restent disponibles pour diagnostic.
- **Nettoyage** : suppression `flash_usb_coredump.ps1`, `partitions_esp32_wroom_ota_coredump.csv`, `partitions_esp32_wroom_test_coredump.csv`, `partitions_esp32_s3_test_coredump.csv`.
- **Nouvelle partition** : `config/partitions/partitions_esp32_wroom_test.csv` (spiffs 0xF0000, sans coredump).
- **Fichiers** : `platformio.ini`, partitions, `tools/coredump/README.md`, `docs/technical/COREDUMP_PANIC_S3.md`, `docs/INVENTAIRE_SCRIPTS_FFP5CS.md`.

---

## Version 12.43 - 2026-03-12

### Incrémentation pour OTA (audit échanges firmware-serveur)
- **Résumé** : Version incrémentée pour publication OTA après audit serveur (suppression cache OutputCacheService, StateNormalizer, doc scénarios asymétriques).

---

## Version 12.42 - 2026-03-12

### Phase 2 : 3 slots POST par catégorie (priorité 3 > 2 > 1)

- **Slots réservés** : un slot par catégorie POST — cat3 replay (priorité haute), cat2 ack/événements, cat1 données périodiques. Isolation pour éviter qu’une catégorie bloque les autres en saturation.
- **Layout WROOM** : slot 7 OTA, 6 Fetch, 5 cat3, 4 cat2, 3 cat1, 0..2 partagés (Heartbeat, fallback).
- **API** : `netPostRaw(payload, timeout, PostCategory, outFailure)` ; `sendFullUpdate` et `sendCommandAck` propagent la catégorie.
- **Fichiers** : `app_tasks.cpp`, `post_category.h`, `automatism_sync.cpp`, `sd_logger.cpp`, `automatism.h/cpp`.

---

## Version 12.41 - 2026-03-12

### Corrections nourrissage automatique (audit 12 mars 2026)

- **_feedingPhaseEnd** : inclut désormais la phase Petits (durée totale = gros + délai + petits). Évite que `isFeedingInProgress()` repasse à `false` pendant la phase Petits.
- **Sync serveur** : déplacée de `feedingCompleteCallback` (appelée trop tôt) vers `finalizeFeedingIfNeeded` — envoi des flags `bouffePetits=1` / `bouffeGros=1` lorsque le cycle est **réellement** terminé.
- **Constante** : `FEEDING_DELAY_BETWEEN_SEC` dans `automatism_feeding_schedule.h` (alignée avec `feedSequential`).
- **Logs NDJSON** : retirés de `checkNewDay`, `checkAndFeed`, `loadBouffeFlags`, `saveBouffeFlags`, `handleFeeding`.
- **Fichiers** : `automatism.cpp`, `automatism_feeding_schedule.cpp`, `automatism_feeding_schedule.h`, `config.cpp`.

---

## Version 12.40 - 2026-03-12

### Garantie envoi POST régulier (slot réservé + throttle strict)

- **Slot réservé POST** : `netRequestAllocForPost()` tente le slot N-4 en priorité (WROOM: slot 4) pour les POST données capteurs. Heartbeat et Fetch n’utilisent pas ce slot.
- **Throttle strict** : seuil `netRequestPoolPostSlotsFullThreshold()` (WROOM: 6) — différer POST quand tous les slots POST sont occupés, au lieu de `poolSize - 1` (7).
- **Effet** : en saturation du pool, au moins un POST peut toujours être envoyé via le slot réservé. Réduction des tentatives inutiles.
- **Fichiers** : `app_tasks.cpp`, `automatism_sync.cpp`, `app_tasks.h`.

---

## Version 12.39 - 2026-03-11

### Optimisation CPU : période webTask 150 → 200 ms (scénario 5)

- **Changement** : période de polling webTask portée de 150 ms à 200 ms. Gain ~50 % d'appels en moins sur webServer.loop() par rapport à l'original (100 ms).
- **Effet** : réduction de la charge CPU et des commutations de tâche sur core 1.

---

## Version 12.38 - 2026-03-11

### Correctif stack overflow netTask wroom-prod (Stack canary)

- **Contexte** : wroom-prod en crash loop (19 Guru Meditation en 5 min), `Stack canary watchpoint triggered (netTask)` lors des opérations TLS/OTA au boot.
- **Changement** : `NET_TASK_STACK_SIZE` porté de 12 KB à 16 KB pour WROOM (aligné S3). La stack S3 avait déjà été augmentée pour le même symptôme (mbedTLS/HTTP gourmands).
- **Validation** : monitor 5 min, 0 crash, 0 reboot. RAM wroom-prod ~36 %, flash 86,4 %.

---

## Version 12.37 - 2026-03-10

### Correctif crash LoadProhibited AP de secours (monitoring 20 min 2026-03-10)

- **Garde heap en entrée de startFallbackAP()** : vérification `ESP.getFreeHeap() >= MIN_HEAP_AP_MODE` (10 KB) **avant** tout appel à `WiFi.mode()` ou `WiFi.softAP()`. La garde v12.33 protégeait uniquement le scan ; le crash survenait dans `WiFi.mode(WIFI_AP)` quand la heap était ~6 KB.
- **Constante** `MIN_HEAP_AP_MODE = 10240` (10 KB) dans `NetworkConfig`.
- **Effet** : si heap < 10 KB au moment de l'échec WiFi, l'AP de secours n'est pas démarré (return false), le système reste en offline ; la prochaine tentative de connexion réessaiera plus tard.

---

## Version 12.36 - 2026-03-10

### Optimisation CPU : période webTask 100 → 150 ms

- **Scénario 5** : réduction de la fréquence de polling de webTask de 100 ms à 150 ms. Gain estimé ~33 % d'appels en moins sur webServer.loop() (WebSocket + broadcast). Validation : run 15 min avec client WebSocket connecté.

---

## Version 12.35 - 2026-03-10

### Correctif timeouts RPC POST (S3)

- **RPC >= HTTP + marge** : `HTTP_POST_RPC_TIMEOUT_MS` porté à 17 s (S3) pour respecter la contrainte « RPC timeout >= HTTP timeout + 2 s ». Évite que l'appelant abandonne avant la fin réelle du POST (10 s < 15 s provoquait des abandons prématurés).

---

## Version 12.34 - 2026-03-10

### Correctif DS3231 (WROOM-S3) : bit century et réparation au boot

- **Bit century inversé** : convention DS3231 — bit 1 = 2000s, bit 0 = 1900s. Lecture et écriture corrigées. Élimine les faux « 1900-02-01 » au démarrage.
- **Réparation au boot** : si lecture DS3231 invalide, écriture de l'heure NVS valide dans le RTC (utile si batterie morte ou RTC jamais initialisé).

---

## Version 12.33 - 2026-03-10

### Correctifs monitoring 15 min (crash AP, pool NetRequest, heap)

- **Crash LoadProhibited AP de secours** : garde heap dans `startFallbackAP()` — si heap < 12 KB (`MIN_HEAP_AP_SCAN`), skip `WiFi.scanNetworks()` et utilisation canal 6 direct (évite allocations internes qui échouent).
- **Pool NetRequest saturé** : timeouts RPC réduits pour libérer slots plus tôt — `HTTP_POST_RPC_TIMEOUT_MS` 16→10 s (S3), `FETCH_REMOTE_STATE_RPC_TIMEOUT_MS` 20→12 s. Throttle renforcé : `poolUsed >= poolSize - 1`.
- **Protection heap** : avant POST dans `AutomatismSync::update()`, vérifier `ESP.getFreeHeap() >= 20 KB`. Si insuffisant, différer et logger une fois par minute.

---

## Version 12.32 - 2026-03-10

### Correctif overflow DRAM (region dram0_0_seg) sur wroom-test

- **Contexte** : le link échouait avec « region dram0_0_seg overflowed by 3696 bytes » sur wroom-test (problème préexistant).
- **Changements** (WROOM uniquement, conformité règles cœur et HEAP_RECOVERY_OPTIONS) :
  - Pool NetRequest : 10 → 8 slots (saves ~2 KB). Slots 0..5 = POST/Heartbeat, 6 = Fetch, 7 = OTA.
  - POST_PAYLOAD_MAX_SIZE : 1024 → 896 octets (payload sync typ. < 800 bytes).
  - REMOTE_JSON_CACHE_SIZE (config, deferred) : 2048 → 1536 octets. GET outputs/state typ. < 900 bytes.
- **Effet** : wroom-test compile et linke. RAM ~38 %. S3 inchangé.

---

## Version 12.29 - 2026-03-10

### Slot réservé FetchRemoteState dans le pool netRPC

- **Contexte** : lorsque le pool netRPC est saturé par des POSTs (15/16 sur S3, 9/10 sur WROOM), `fetchRemoteState` échouait systématiquement et les commandes distantes (GPIO, nourrissage) n’étaient plus appliquées.
- **Changement** : slot N-2 réservé à FetchRemoteState (poll/GET outputs/state), comme le slot N-1 pour l’OTA. `netRequestAllocForFetch()` tente ce slot en priorité. POST/Heartbeat utilisent les slots 0..N-3.
- **WROOM** : slot 9 (index) = OTA (« slot 10 »), slot 8 = Fetch, slots 0..7 = POST/Heartbeat.
- **S3** : slot 15 = OTA, slot 14 = Fetch, slots 0..13 = POST/Heartbeat.
- **Effet** : poll et GET s’exécutent toujours, même lorsque le pool est saturé.

---

## Version 12.28 - 2026-03-10

### Diagnostic : fetchRemoteState InvalidInput

- **Contexte** : GET `/ffp3/api/outputs3-test/state` renvoie HTTP 200 (968 bytes) mais ArduinoJson échoue avec `InvalidInput` au parsing.
- **Changement** : ajout d’un dump de diagnostic quand le parse échoue (3 premières occurrences ou toutes les 30 s) : hex des 16 premiers octets, 80 premiers caractères imprimables, 60 derniers si totalRead > 120. Permet d’identifier BOM, en-têtes chunked, ou contenu non JSON.

---

## Version 12.27 - 2026-03-05

### Debug : instrumentation nourrissage automatique

- **Contexte** : investigation des nourrissages auto intempestifs (session debug 9f8a7c).
- **Changement** : logs NDJSON temporaires dans `checkNewDay`, `checkAndFeed`, `saveBouffeFlags`, `loadBouffeFlags`, `handleFeeding` pour valider les hypothèses (jour, flags, race, RTC, réécriture). À retirer après identification et correctif.

---

## Version 12.26 - 2026-03-05

### OTA : slot dédié dans le pool netRPC

- **Contexte** : lorsque le pool netRPC est saturé (ex. 16/16 sur S3), la « Demande vérification OTA après réveil » échouait systématiquement (« OTA check renoncé: pool plein »), sans tentative de GET metadata.
- **Changement** : un slot du pool est réservé à l’OTA (dernier slot, index N-1). `netRequestAlloc(false)` n’utilise que les slots 0..N-2 ; `netRequestOtaCheck()` appelle `netRequestAlloc(true)` qui tente d’abord ce slot dédié, puis le reste du pool en secours.
- **Effet** : la vérification OTA (après réveil, trigger distant, timer 2 h) peut toujours obtenir un slot même si les autres requêtes (Sync, GET state, Mail) saturent le pool. WROOM : 9 slots usage normal + 1 OTA ; S3 : 15 + 1.

---

## Version 12.25 - 2026-03-05

### OTA : buffer metadata et metadata minifié côté serveur

- **Contexte** : le JSON metadata OTA servi (~2,7 Ko) dépassait le buffer de réception ESP (2048 octets), provoquant troncature puis `IncompleteInput` au parsing.
- **Firmware** : buffer payload metadata porté à 3072 octets (`OTA_METADATA_PAYLOAD_BUFFER_SIZE` dans `BufferConfig`, WROOM et S3) pour accepter le metadata actuel avec marge.
- **Publication OTA** : le script `publish_ota.ps1` écrit `metadata.json` en JSON compact et omet les entrées `channels.*.default` pour rester sous 2048 octets ; les ESP en firmware antérieur peuvent ainsi parser le metadata et faire l’OTA vers 12.25. Détail : `docs/technical/OTA_PUBLISH.md` section « Contrainte taille metadata ».

---

## Version 12.24 - 2026-03-05

### Robustesse : éviter _sensors.read() dans les chemins d'envoi serveur (automatism)

- **Contexte** : les capteurs défaillants ou lents (ex. DHT) peuvent faire bloquer `sensors.read()` jusqu’à 5 s. Les appels à `_sensors.read()` depuis l’automatisme (pour envoyer l’état au serveur) bloquaient automationTask et exposaient au WDT.
- **Changements** : tous les envois serveur depuis l’automatisme utilisent les lectures déjà disponibles au lieu de `_sensors.read()` :
  - **Pompe / remplissage** : `handleRefillAquariumOverfillSecurity`, `handleRefillAutomaticStart`, `handleRefillManualCycleEnd`, `handleRefillMaxDurationStop` utilisent le paramètre `r` (lectures du cycle courant).
  - **Fin de nourrissage auto** : callback utilise `_lastReadings` (mis à jour par `update(r)`).
  - **toggleEmailNotifications**, **updateDisplay()**, **startTankPumpManual()** : utilisent `getLastCachedReadings()` avec repli sur `_lastReadings` (pas de lecture bloquante).
- **Effet** : automationTask ne bloque plus dans une lecture capteur lors des sync serveur ; capteurs défaillants n’impactent plus la stabilité ni netTask.

---

## Version 12.23 - 2026-03-04

### Fix WiFi : compatibilité routeurs 4G (inwi Home 4G / Huawei)

- **MAC override système** : `overrideBaseMac()` via `esp_base_mac_addr_set()` dérive un MAC localement administré depuis le MAC eFuse usine, appliqué avant `WiFi.mode()`. Contourne le filtrage OUI Espressif par certains routeurs 4G.
- **TX power réduit à 12.5 dBm** : `esp_wifi_set_max_tx_power(50)` au lieu de 82 (20.5 dBm). Corrige AUTH_EXPIRE persistant avec les routeurs 4G type inwi Home 4G qui rejettent les clients WiFi à puissance TX élevée.
- **Nettoyage** : suppression du wrapper `wifiBeginPMF` (hypothèse PMF rejetée), remplacé par `wifiBeginSafe` simplifié. Suppression des logs de debug `[DBG-289e1f]`.

---

## Version 12.22 - 2026-03-04

### SD Data Logger : stockage offline et replay (BOARD_S3 uniquement)

- **Nouveau module `sd_logger`** : stocke chaque payload POST sur carte micro SD en fichiers journaliers (`/sdcard/log/YYYYMMDD.csv`) et file d'attente persistante (`/sdcard/queue/SEQNUM.dat`).
- **Idempotence** : champ `post_id` (BOARD_TYPE-epoch-seq) ajouté à tous les payloads POST pour déduplication côté serveur.
- **Replay offline** : à la reconnexion WiFi, les POSTs non confirmés sont rejoués depuis la SD (max 5 par cycle dans autoTask).
- **Rotation automatique** : les fichiers log de plus de 30 jours sont supprimés au boot.
- **Endpoints locaux** : `GET /api/history?date=YYYYMMDD` (données historiques pour graphiques) et `GET /api/sd-status` (état SD + pending count).
- **Serveur distant** : déduplication par `post_id` dans `PostDataController`, colonne `post_id` VARCHAR(64) UNIQUE nullable dans les tables ffp3Data*.
- **Isolation S3** : tout le code SD est conditionné par `#if defined(BOARD_S3)`, stubs vides sur WROOM. Le `post_id` est ajouté sur tous les boards.

---

## Version 12.21 - 2026-03-04

### Réseaux WiFi lents / faibles : timeouts reconnexion et stabilisation

- **Reconnexion après réveil** : nouveau timeout dédié `WIFI_RECONNECT_AFTER_WAKE_MS` (8 s) dans `power.cpp` pour laisser plus de temps à l’association et au DHCP sur liens lents (dérogation par rapport à 5 s).
- **Stabilisation réseau** : `waitForNetworkReady()` utilise 5 s max pour le DNS au lieu de 3 s (réseaux lents).
- **Documentation** : commentaire corrigé dans `web_server.cpp` (connexion manuelle WiFi utilise `WIFI_CONNECT_ATTEMPT_TIMEOUT_MS`).

---

## Version 12.20 - 2026-03-04

### netRPC : pool 16/10 + timeouts RPC réduits

- **Pool netRPC** : S3 12→16, WROOM 8→10 pour réduire la saturation sous charge (run 5 min 15:13 : used=12/12).
- **Timeouts RPC** : `FETCH_REMOTE_STATE_RPC_TIMEOUT_MS` 30 s→20 s ; `HTTP_POST_RPC_TIMEOUT_MS` S3 20 s→16 s, WROOM 26 s→22 s. Libération des slots plus tôt en cas de serveur lent.
- Déploiement OTA wroom-prod et wroom-s3-test.

---

## Version 12.19 - 2026-03-04

### netRPC : pool agrandi + throttle quand quasi plein

- **Pool netRPC** : taille augmentée (WROOM 6→8, S3 8→12) et file `g_netQueue` alignée pour limiter les échecs « Pool plein » et « POST échoué (file pleine…) » quand le serveur est lent ou la charge élevée.
- **Throttle Sync** : dans `AutomatismSync::update()`, si le pool est quasi plein (`used >= size - 2`), on diffère replay et sendFullUpdate pour éviter de saturer ; log « POST différé: pool netRPC quasi plein » au plus une fois par minute.
- **API** : `AppTasks::netRequestPoolUsedCount()` et `AppTasks::netRequestPoolSize()` exposées pour le throttle (et diagnostic).

---

## Version 12.18 - 2026-03-04

### Maintenance

- Incrément version pour déploiement OTA (wroom-prod, wroom-s3-test).

---

## Version 12.17 - 2026-03-04

### OTA au boot prioritaire

- **netTask** : la vérification OTA au boot s’exécute **avant** la récupération de la config serveur (première opération HTTPS après WiFi + délai TLS). Plus de dépendance à `bootServerReachable` : l’OTA est tenté dès que WiFi est connecté et la heap suffisante, ce qui renforce la faisabilité de la mise à jour au démarrage. Comportement offline-first et timeouts inchangés.

---

## Version 12.16 - 2026-03-04

### Corrections run 5 min (wroom-s3-test)

- **Stack autoTask (P2)** : `AUTOMATION_TASK_STACK_SIZE` porté à 12 KB pour BOARD_S3 uniquement (10 KB conservé pour WROOM) ; évite les alertes « Stack > 70 % » (HWM 78 % observé).
- **Mail skip heap (P3)** : `MIN_HEAP_BLOCK_FOR_MAIL_TLS` abaissé à 32000 pour S3 pour accepter un bloc observé à 32756 sans risque TLS.
- **OTA heap (P4)** : si heap insuffisante au boot, OTA reporté avec log « retry dans 60 s » ; retry automatique une fois après 60 s dans netTask (sans boucle bloquante). Log explicite dans ota_manager : « [OTA] Reporté au prochain cycle (heap) ».
- **Doc coredump** : procédure `docs/technical/COREDUMP_PANIC_S3.md` pour extraire et analyser le coredump après un PANIC sur S3 (offset partition 0xE00000).

---

## Version 12.15 - 2026-03-03

### Maintenance

- Vérification et mise à jour de la documentation (plan doc) : cohérence VERSION.md / config.h, règles projet, liens, clés NVS.

---

## Version 12.14 - 2026-03-03

### Maintenance

- Incrément version, build wroom-s3-test, publication OTA.

---

## Version 12.13 - 2026-03-03

### Maintenance

- Incrément version après flash wroom-s3-test et déploiement OTA.

---

## Version 12.12 - 2026-03-03

### OTA distant pour wroom-s3-test

- **OTA au boot et périodique (2 h) activés pour wroom-s3-test** : la condition `!defined(PROFILE_TEST)` est assouplie en `!defined(PROFILE_TEST) || defined(BOARD_S3)`, afin que les envs S3 (wroom-s3-test, wroom-s3-test-psram, etc.) bénéficient de l’OTA automatique. wroom-test (WROOM + PROFILE_TEST) conserve l’OTA manuel uniquement (évite blocage netTask > TWDT). Doc `docs/technical/OTA_PUBLISH.md` mise à jour.

---

## Version 12.05 - 2026-02-21

### Maintenance

- Incrément version (config.h + VERSION.md), commit et push GitHub

---

## Version 12.02 - 2026-02-19

### Corrections

- **Publication données après plusieurs heures** : exécution de la synchro (poll + POST) aussi en branche timeout de la queue capteurs, avec les dernières lectures (`s_lastReadings`), pour ne pas couper la publication lorsque des timeouts se répètent (hypothèse H1).

---

## Version 11.193 - 2026-02-03

### Maintenance

- Incrément version (config.h + VERSION.md), commit et push GitHub

---

## Version 11.190 - 2026-02

### Points à surveiller

- HTTP GET/Heartbeat 5s ; **POST 8s** (dérogation `HTTP_POST_TIMEOUT_MS`).
- NVS remove idempotent, servo pin valide

---

## Version 11.167 - 2026-01-30

### Maintenance

- ✅ **Build tous environnements** : wroom-prod, wroom-test, wroom-beta, wroom-tls-test
- ✅ **Version documentée** : 11.167 dans config.h et VERSION.md
- ✅ **Push GitHub** : commit et push après validation build

---

## Version 11.155 - 2026-01-17

### Corrections critiques

- ✅ **Correction duplication constantes EPOCH** : Utilisation d'alias dans `SleepConfig` vers `SystemConfig` pour éviter duplication
- ✅ **Documentation version** : Ajout de la version 11.155 dans l'historique

---

## Version 11.156 - 2026-01-25

### Maintenance

- ✅ **Incrémentation version** : Passage à la version 11.156

---

## Version 11.138 - 2026-01-15

### Réseau / stabilité

- ✅ **HTTP au boot** : suppression du GET distant pendant l'init NTP pour éviter un panic LWIP

---

## Version 11.137 - 2026-01-15

### Mail / Filesystem

- ✅ **ESP Mail Client** : flash FS désactivé pour éviter les erreurs `spiffs`

---

## Version 11.136 - 2026-01-15

### Stockage LittleFS

- ✅ **Montage stable** : base path `/littlefs` explicite pour éviter l'échec d'enregistrement VFS
- ✅ **Montage partagé** : label `littlefs` conservé sur tous les points d'entrée

---

## Version 11.135 - 2026-01-15

### Stockage LittleFS

- ✅ **Montage fiable** : label `littlefs` explicite dans tous les modules
- ✅ **Format cohérent** : remontage après format avec le bon label

---

## Version 11.134 - 2026-01-14

### Remplissage manuel (pompe réserve)

- ✅ **Mode manuel fiable** : timer + décompte initialisés au démarrage
- ✅ **Commande distante robuste** : front montant/descendant pour éviter les retriggers
- ✅ **Sécurité d'état** : ignore les commandes redondantes

---

## Version 11.133 - 2026-01-14

### Nourrissage manuel

- ✅ **Reset centralisé** : fin de phase gérée dans le cœur (indépendant OLED)
- ✅ **Sync flags distants** : remise à 0 après commandes serveur (cooldown)
- ✅ **OLED** : suppression du reset métier pour éviter dépendance écran

---

## Version 11.132 - 2026-01-14

### OLED

- ✅ **Ecran relais** : ajout des heures/temps de nourrissage et des seuils (aq, tank, chauffage, flood)

---

## Version 11.130 - 2026-01-14

### Stabilité & robustesse (prod)

#### Correctifs critiques
- ✅ **WiFi** : suppression du double pilotage (un seul `wifi.loop()` dans `src/app.cpp`)
- ✅ **Capteurs** : prise en compte explicite des `NaN` (évite propagation silencieuse)
- ✅ **Serial en PROD** : désactivation sûre via stub `NullSerial` (réduction flash significative)
  - wroom-prod flash : ~96.9% → ~94.6% après compilation

#### Correctifs complémentaires
- ✅ **Digest email** : clés NVS harmonisées (`digest_last_seq`, `digest_last_ms`) pour éviter la perte d'état
- ✅ **/dbvars** : séparation des heures de nourrissage et des flags d'état (`bouffeMatinOk`, etc.)
- ✅ **Pompe réservoir** : statistiques fiabilisées (runtime courant + compteur d'arrêts)
- ✅ **NVS** : `forceFlush()` écrit directement pour éviter la ré-entrée
- ✅ **WiFi** : connexion manuelle centralisée via `WifiManager::connectTo()`

#### Nettoyage / cohérence
- ✅ **WatchdogManager** : suppression du module non utilisé (réduction surface de bugs + taille)
- ✅ **Config** : headers historiques `include/config/*` convertis en wrappers vers `include/config.h` (évite divergence)

#### Tests
- ✅ **Tests natifs (PlatformIO env:native)** : remise en état des suites unitaires + mocks Arduino minimalistes
  - `RateLimiter` et `TimerManager` passent sur hôte

---

## Version 11.129 - 2026-01-13

### Core Dump - Outils d'extraction et analyse

**Amélioration du système de debugging avec core dump**

#### Nouvelles fonctionnalités
- ✅ **Outils d'extraction** : Création de scripts Python pour extraire les core dumps depuis la flash
  - `tools/coredump/extract_coredump.py` - Extraction depuis la partition flash
  - `tools/coredump/analyze_coredump.py` - Analyse avec stack trace
  - `tools/coredump/coredump_tool.py` - Outil intégré (extraction + analyse)
  - Documentation complète dans `tools/coredump/README.md`

#### Corrections de configuration
- ✅ **Build flags harmonisés** : Ajout de `CONFIG_ESP_COREDUMP_ENABLE=1` dans `wroom-prod`
  - Cohérence entre environnements test et production
  - Fichier modifié : `platformio.ini`
  
- ✅ **Offsets de partition corrigés** : Remplacement des offsets automatiques par valeurs explicites
  - Calcul manuel des offsets pour éviter les erreurs
  - Fichier modifié : `partitions_esp32_wroom_ota_coredump.csv`
  - Offsets: app1=0x1B0000, littlefs=0x350000, coredump=0x3F0000

#### Documentation
- ✅ **Guide d'utilisation** : Création de `docs/guides/COREDUMP_USAGE.md`
  - Procédures d'extraction et d'analyse
  - Interprétation des résultats
  - Dépannage
- ✅ **Rapport d'analyse** : Création de `docs/reports/RAPPORT_ANALYSE_PARTITION_COREDUMP.md`
  - Analyse complète de la configuration core dump
  - Problèmes identifiés et solutions
  - Plan d'action et recommandations

---

## Version 11.128 - 2026-01-13

### Corrections de conformité .cursorrules

**Conformité améliorée de 75% à 90%+**

#### Corrections critiques
- ✅ **WebSocket** : Ajout de vérification `connectedClients() > 0` avant chaque envoi (8 occurrences)
  - Évite les envois inutiles quand aucun client connecté
  - Fichier modifié : `include/realtime_websocket.h`
  
- ✅ **Gestion mémoire** : Refactorisation de `buildSystemInfoFooter()` dans `mailer.cpp`
  - Remplacement de `String` par buffer statique `char[]` avec `snprintf()`
  - Réduction de la fragmentation mémoire
  - Fichier modifié : `src/mailer.cpp`

#### Corrections importantes
- ✅ **Conventions de nommage** : Uniformisation du préfixe `g_` pour variables globales
  - `realtimeWebSocket` → `g_realtimeWebSocket`
  - `autoCtrl` → `g_autoCtrl`
  - Fichiers modifiés : Tous les fichiers utilisant ces variables
  
- ✅ **Watchdog et stabilité** : Remplacement de `delay(5000)` par `vTaskDelay()` dans `power.cpp`
  - Conforme à la règle : "Utiliser `vTaskDelay()` au lieu de `delay()` dans les tâches"
  - Fichier modifié : `src/power.cpp`

#### Documentation
- ✅ Création du fichier `VERSION.md` pour documenter les changements de version

---

## Template pour les prochaines versions

```markdown
## Version X.XX - YYYY-MM-DD

### Nouvelles fonctionnalités
- Description de la nouvelle fonctionnalité

### Corrections
- Description de la correction de bug

### Améliorations
- Description de l'amélioration/optimisation

### Changements techniques
- Description des changements techniques (refactoring, architecture, etc.)

### Notes
- Notes importantes pour cette version
```

---

## Notes importantes

- **OBLIGATOIRE** : Incrémenter la version dans `include/config.h` avant chaque déploiement
- **OBLIGATOIRE** : Faire un monitoring de 90 secondes après chaque déploiement
- **OBLIGATOIRE** : Analyser les logs complets après chaque déploiement
- Documenter chaque changement de version dans ce fichier
- Faire des commits atomiques avec numéro de version (ex: "v11.127: Fix WebSocket checks")

---

## Références

- Configuration de version : `include/config.h` → `ProjectConfig::VERSION`
- Règles de développement : `.cursor/rules/` (ex. règles cœur FFP5CS)
- Rapport d'adéquation : `docs/reports/RAPPORT_ADEQUATION_CURSORRULES_CODE.md`
