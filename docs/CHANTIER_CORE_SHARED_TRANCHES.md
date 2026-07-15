# Chantier « core architectural partagé » — état livré & cahier des charges des tranches suivantes

> **Rôle de ce document** : permettre à une nouvelle session (ou un nouveau contributeur)
> de reprendre le chantier de mutualisation **exactement** là où il s'est arrêté, en
> suivant le même cahier des charges. Il complète (et ne remplace pas)
> **`docs/PROPOSITION_REFACTORISATION_SHARED.md`** — l'analyse complète (3 axes,
> vérification adversariale, matrice « meilleur de la famille », plan L1→L7) — qu'il
> faut lire en premier. Le prompt de reprise prêt à l'emploi est dans
> **`docs/PROMPT_REPRISE_CHANTIER_SHARED.md`**.

## 1. Contexte et objectif

Monorepo de firmwares ESP32 d'une même famille :

- **Cohorte deep-sleep** : `n3pp/`, `msp/`, `uploadphotosserver/` (CAM), `poissonglouton/`
  — mono-boucle `setup()` → deep sleep, état en `RTC_DATA_ATTR`.
- **Cohorte FreeRTOS** : `ffp5cs/` — multi-tâches, mutex TLS, light sleep. Elle
  **contribue** des briques au core mais ne partage pas le squelette applicatif.

Objectif : un **core partagé en 2 anneaux** — Anneau 1 = primitives universelles dans
`shared/` (consommées par tous, y compris ffp5cs) ; Anneau 2 = framework applicatif
`n3_app` (cohorte deep-sleep uniquement, à terme). Principe : **prendre le meilleur de
chaque firmware** (logique pure/robustesse ← ffp5cs ; offline-first/upload ←
uploadphotosserver ; squelette deep-sleep ← n3pp) et **remonter la primitive pure vers
`shared/`**, presque jamais remplacer un module ffp5cs par shared.

## 2. État livré — PR #86 (MERGÉE dans master)

La première PR du chantier (`docs: proposition…` + tranches L1/L1b/L2) a été **mergée**.
Commits livrés (dans l'ordre) :

| Commit | Contenu |
|---|---|
| `d9e2665`, `ccf1324`, `c686c30`, `71884f8` | Document d'analyse `PROPOSITION_REFACTORISATION_SHARED.md` : 3 axes, **vérification adversariale** (pièges A6/A7/A8/A10…), feuille de route core, **plan consolidé 4 firmwares + veille externe** (Partie II, lots L1→L7) |
| `82fbb18` | **L1 (additif)** : 6 briques de logique pure copiées de ffp5cs vers `shared/` (octet-identique, namespaces conservés) + 6 suites Unity portées + stub `esp_system.h` + CI |
| `049aa23` | **L1b (dédup ffp5cs)** : ffp5cs consomme les 6 headers shared, ses copies locales `include/*.h` supprimées, tests natifs repointés, `platformio-native.ini` +2 `-I` |
| `7e0ff49` | **L2 (additif)** : brique HMAC canonique `shared/n3_hmac/src/n3_hmac_canonical.{h,cpp}` (`n3hmac::computeHmacHex` sans `String`, TU sans HTTPClient/Arduino) + `test_hmac_canonical` + stub `esp_random.h` |
| `c28a5fd` | **L2 (câblage)** : les 3 implémentations HMAC dédupliquées — `n3_data.cpp` (suppression `String signedMsg` du chemin chaud), `ffp5cs/hmac_sign.cpp` (délègue), `uploadphotosserver/camera_uploader.cpp` (variante `body = api_key`) — **sans changement observable** (chaque appelant garde son nonce) |

**Briques shared livrées** :

| Fichier | Namespace | Origine |
|---|---|---|
| `shared/n3_time/src/n3_epoch_util.h` | `EpochUtil` | ffp5cs `epoch_util.h` |
| `shared/n3_time/src/n3_clock_decision.h` | `ClockDecision` | ffp5cs `clock_decision.h` |
| `shared/n3_time/src/n3_uptime_format.h` | `UptimeFormat` | ffp5cs `uptime_format.h` |
| `shared/n3_common/src/n3_sleep_decision.h` | `SleepDecision` | ffp5cs `sleep_decision.h` |
| `shared/n3_common/src/n3_reset_reason.h` | `ResetReason` | ffp5cs `reset_reason.h` |
| `shared/n3_common/src/n3_login_throttle.h` | `LoginThrottle` | ffp5cs `login_throttle.h` |
| `shared/n3_hmac/src/n3_hmac_canonical.{h,cpp}` | `n3hmac` | ffp5cs `hmac_sign.cpp` (calcul) |

**Versions au moment du merge** : `n3_time 1.1.0`, `n3_common 1.5.0`, `n3_hmac 1.1.0`,
`n3_data 1.2.2`, ffp5cs `15.13`, uploadphotosserver `2.63`.
⚠️ **Master évolue en parallèle** (au moment d'écrire : ffp5cs déjà `15.15`, envs
`*-https` ajoutés en CI, nouvelle lib `n3_tracker`) → toujours raisonner sur l'état
**actuel** de master, pas sur les numéros figés ici.

## 3. Protocole de vérification de la PR #86 (phase 0 de toute reprise)

Avant d'écrire la moindre ligne, la session de reprise DOIT :

1. **CI verte post-merge** : lister les runs `firmware-ci.yml` sur `master` depuis le
   merge de #86 → `completed/success` attendu (job « Tests natifs (Unity) » : suites
   shared dont `test_hmac_canonical`, `test_epoch_util`… + suites ffp5cs + pgl ; job
   « Build » : toutes cibles y compris `uploadphotosserver-{msp1,n3pp,ffp3}` et
   ffp5cs WROOM/S3/prod). Si un run est rouge : **diagnostiquer et corriger avant tout**.
2. **Relecture ciblée du diff mergé** (points sensibles) :
   - `shared/n3_data/src/n3_data.cpp` : bloc X-Sig (~l.124-140) appelle bien
     `n3hmac::computeHmacHex(cfg.sigSecret, tsBuf2, nonceBuf, body.c_str(), …)`,
     la signature legacy `n3HmacSha256(…, tsBuf, …)` (~l.92) est **inchangée** ;
   - `ffp5cs/src/hmac_sign.cpp` : ne contient plus de mbedtls direct, délègue à
     `n3hmac::` ; `web_client.cpp` non modifié ; constantes `HmacSign::*` = 64/65/16/17 ;
   - `uploadphotosserver/src/camera_uploader.cpp` : `n3hmac::computeHmacHex(…,
     api_key)` = même condensé qu'avant (`ts\n nonce\n api_key`), nonce epoch-compteur conservé ;
   - ffp5cs : plus AUCUN `#include` des 6 anciens basenames (`epoch_util.h`, etc.) hors
     mentions historiques VERSION.md/docs ; fichiers `ffp5cs/include/{epoch_util,clock_decision,uptime_format,sleep_decision,reset_reason,login_throttle}.h` absents ;
   - `shared/tests_native/test/test_data/test_data.cpp` inclut `n3_hmac_canonical.cpp`.
3. **Budgets flash ffp5cs** : les post-scripts `pio_check_flash_budget.py` passent en CI
   (le tirage de la lib `n3_time` est neutralisé par `--gc-sections`).
4. **Comportement runtime** : rien à retester sur cible pour L1/L1b/L2 (logique
   octet-identique + digest identique), mais toute anomalie serveur 401 sur POST signés
   serait le signal d'un problème HMAC → vérifier en priorité le point 2.

## 4. Cahier des charges — règles invariantes du chantier

**Méthode par tranche** (non négociable) :
1. Tranche **additive** d'abord (nouvelle brique dans `shared/` + tests natifs +
   branchement CI, AUCUN consommateur) → push → **CI verte**.
2. Puis tranche de **câblage** (consommateurs délèguent / adoptent) → push → CI verte.
   Un câblage ne doit produire **aucun changement observable** (mêmes octets envoyés,
   mêmes logs métier, même timing) sauf décision explicite documentée.
3. Chaque tranche = commit(s) atomique(s) avec message expliquant la non-régression.

**Vérification** : PlatformIO n'est PAS installé dans les sessions distantes → **la CI
est le vérificateur** (`.github/workflows/firmware-ci.yml`). Toute nouvelle suite de
test doit être ajoutée à la boucle `for s in …` du job natif. Les tests portés depuis
ffp5cs gardent **les mêmes assertions** (parité). S'abonner à la PR et corriger tout rouge.

**Versionnage** (à chaque tranche) : bump `library.json` des libs shared touchées
(semver) + table `shared/README.md` + version du/des firmware(s) touché(s) via le skill
`bump-firmware-version` (`versionSource` dans `firmwares.manifest.json`) + entrée
`VERSION.md` du firmware. Mettre à jour `firmwares.manifest.json` si la topologie change.

**Contraintes transverses** :
- **3 toolchains** : WROOM pioarduino arduino-esp32 3.3.x (n3pp/msp/ffp5cs-WROOM/upload…
  vérifier l'état courant), S3 espressif32@6.13.0 (2.0.x), esp32cam espressif32@6.13.0.
  Toute brique Anneau 1 compile sous les trois (pièges : ArduinoJson v6/v7, ESP32Servo).
- **Zéro `String` en chemin chaud** (DRAM ffp5cs WROOM ~99,9 %) ; buffers pile + snprintf.
- **PSRAM** : ne jamais utiliser `psramFound()` (faux sur esp32cam) →
  `heap_caps_get_total_size(MALLOC_CAP_SPIRAM)`.
- **Secrets** : ne JAMAIS committer `credentials.h`, `ffp5cs/include/secrets*.h` ; la CI
  provisionne depuis les `.example`.
- **Interdits** : `archive/`, `à voir/`. Ne pas « remplacer par shared » les modules
  ffp5cs suivants (architecture async légitime) : `web_client*`, `net_request_pool`,
  `ffp3_post_body`, `ota_manager*`, `power` (light sleep ≠ deep sleep), `nvs_manager*`,
  `wifi_manager` (surcouche AP/S3).

**Pièges de fonctionnement GELÉS** (vérifiés dans le code — cf. section ⚠️ de la
PROPOSITION ; toute tranche qui les touche doit les traiter explicitement) :
- **A6** : `Wificonnect` n3pp appelle `HeureSansWifi()` en échec WiFi (récupération
  horloge NVS) — absent de msp → callbacks toujours injectés par firmware.
- **A7** : msp écrase `PontDiv` par un `analogRead` brut ; n3pp le refuse (audit 4.38).
  `PontDiv` pilote la protection batterie → l'acquisition reste HORS code mutualisé.
- **A10** : clés serveur **104/105 à sens opposé** (n3pp arrosage ↔ msp servo) + clamp
  clé 102 divergent → ne jamais mutualiser un parseur indexé par numéro au-delà de
  100/101/103/106/107/110/112 (et upload a son propre espace 102-106 : exclure).
- **N3NetStats** : `s_stats` statique **sans mutex** → côté ffp5cs, n'enregistrer que
  sous `s_httpMutex` (ou ajouter un lock interne à `n3_net_stats`).
- **Nonce HMAC** : opaque au serveur, mais **conserver le format existant par firmware**
  tant que le serveur n'est pas confirmé agnostique (ffp5cs = aléatoire ; n3_data/upload
  = epoch-compteur).
- **Flag anti-spam batterie** (A8) : n3pp global `emailPontDivSent` ré-armé dans
  `automatismes()` + double-site ; msp static ré-armé en tête de `sommeil()` →
  harmoniser AVANT toute mutualisation de `sommeil()`.

## 5. Tranches suivantes (cahier des charges détaillé)

### T1 — Reste de L2 : primitives data/temps (risque faible) — ✅ LIVRÉ (points 1-2 ; point 3 reporté)

1. **`n3DataSendHeartbeat`** → `shared/n3_data`.
   - Source : `n3pp/src/n3pp_network.cpp` (`sendHeartbeat`, ~l.110-155) et
     `msp/src/msp_network.cpp` (~l.115-160) — **verbatim octet-identique vérifié**
     (diff vide). Champs `uptime/free/min/reboots/rssi`, `static minHeap` (static de
     fonction, non-RTC, réinitialisé à chaque réveil — comportement à conserver).
   - API : `int n3DataSendHeartbeat(const N3HeartbeatConfig&)` avec
     `{url, apiKey, sensorName, version, bootCount, sigSecret, currentEpochSeconds}` —
     tout l'état passe en paramètres. Attention : `minHeap` static → le déplacer dans
     la lib garde la même sémantique (un seul appelant par firmware).
   - Câblage : n3pp et msp remplacent leur corps par l'appel ; suppression des deux
     copies. Bump n3pp/msp + `n3_data`.
   - Test natif : étendre `test_data` (body attendu, gardes) — le POST réseau reste hors périmètre natif.
2. **`n3TimeSyncBrokenDown(rtc, &s,&mi,&h,&j,&mo,&a)`** → `shared/n3_time`.
   - Dédoublonne le resync des 6 globals dupliqué 3× : `print_wakeup_reason` n3pp
     (`n3pp_automation.cpp:~381-403`), msp (`msp_automation.cpp:~197-219`),
     `HeureSansWifi` n3pp (`n3pp_automation.cpp:~12-17`).
3. **Adoption `n3PrintWakeupReason`** — **REPORTÉE (décision de contrat requise)** :
   au-delà de la langue des logs, la version shared charge l'heure NVS **aussi au
   réveil TIMER** (besoin uploadphotosserver, horloge perdue au deep sleep), alors que
   n3pp/msp ne le font qu'en `default` (leur horloge RTC survit). Adoption naïve =
   risque d'écraser une horloge correcte par un epoch NVS stale à chaque réveil timer.
   Options : paramétrer le comportement TIMER (`bool loadNvsOnTimerWake`), ou
   harmoniser côté firmwares après validation terrain. Le resync des 6 globals est
   déjà couvert par `n3TimeSyncBrokenDown` (livré).

### T2 — L3 : robustesse capteurs (risque faible, fort apport) — ✅ LIVRÉ (shared + câblage ffp5cs ; adoption n3pp/msp = tranche ultérieure dédiée)

1. **`sensor_failure_manager`** (ffp5cs `include/sensor_failure_manager.h` + `src/…cpp`)
   → `shared/n3_analog_sensors` (ou module dédié dans la lib).
   - Vérifié : la classe est **100 % paramétrée par constructeur** ; `config.h` n'y sert
     que pour la macro `SENSOR_LOG_PRINTF` → **retirer `config.h`**, injecter la macro
     (paramètre ou hook de log) ; `Arduino.h` légitime (millis()).
   - Méthode : additive (copie + suite native — s'inspirer de l'existant ffp5cs) puis
     câblage ffp5cs (délégation/suppression locale) puis adoption n3pp/msp (remplace le
     fallback ad hoc inférieur de `msp_sensors.cpp:~108-146`) — l'adoption n3pp/msp
     change le comportement (vraie machine d'état) → la documenter comme AMÉLIORATION
     dans VERSION.md, pas comme refacto neutre.
2. **`sensor_reading_fallback.h`** (pur) → même destination. **Renommer** l'API
   `waterLevel`/`resolveWaterLevel` en termes neutres ; côté ffp5cs, garder un alias ou
   adapter les appels. Porter `test_sensor_fallback`.

### T3 — L4 : offline-first & stats (nouvelles libs, risque faible si additive) — ✅ T3 COMPLET (T3a libs+tests ; T3b POST réseau + câblage uploadphotosserver 2.66 ; T3c stats `N3NetStats` branchées dans ffp5cs 15.17 sous `s_httpMutex`)

1. **`n3_upload`** (nouvelle lib `shared/n3_upload/`) — généralise l'upload multipart
   streaming d'uploadphotosserver (`camera_upload.cpp`, `camera_uploader.cpp`).
   - API validée par analyse (voir esquisse complète) : `N3UploadSource` (abstraite :
     `size/rewind/readChunk`) + `N3UploadBufferSource`/`N3UploadStreamSource` ;
     `N3UploadMultipart{boundary, fieldName, filename, contentType}` ;
     `N3UploadConfig{url, apiKey, sigSecret, epoch, headers[], retries, 429-retries,
     timeouts, reconnect cb, onStats cb}` ; `n3UploadMultipart(cfg, part, body)`.
   - **Jamais** de malloc du corps (memcpy/file.read chunké) ; head/tail en buffers pile.
   - **`onStats` callback** (câblé sur `n3NetStatsRecordPost`) — comble l'angle mort :
     les uploads photo échappent aujourd'hui aux stats. Callback injecté, PAS de
     dépendance dure `n3_upload → n3_data`.
   - Testable nativement : sources (offsets head/corps/tail, chunking) avec stub Stream ;
     le POST réseau reste sur cible.
   - Câblage : `camera_uploader.cpp` devient une fine couche métier (headers `X-Sync-*`,
     boundary/fieldName actuels conservés à l'identique).
2. **`n3_store_forward`** (nouvelle lib) — file offline durable, invariants tirés du code :
   - **peek→commit, jamais burn** (upload `cameraSyncPeek/Commit`, `camera_sync.cpp:~190-221`) ;
   - plafond + **drop-oldest** (ffp5cs `web_client_queue.cpp:~40-54`) ;
   - **pacing** (`SYNC_UPLOAD_MIN_INTERVAL_MS`), **budget temps**
     (`SYNC_DRAIN_MAX_DURATION_MS`), **retries 429**, scan borné (`SYNC_MAX_BACKLOG_SCAN`) ;
   - API : `N3SfBackend` (abstrait : `append/count/peek/commit/dropOldest/capacity`),
     verdicts `N3SfSend{Ok, RateLimited, NetworkError, HardFail}`, orchestrateur pur
     `n3SfDrain(backend, cfg, sendCb, ctx)` avec **`nowMs`/`sleepMs` injectés** →
     entièrement testable en natif (backend mock RAM + send scripté). C'est le plus
     gros gain de couverture : ces invariants ne sont testés nulle part aujourd'hui.
   - Backends concrets HORS lib : SD+curseur NVS (upload), NVS indexé (ffp5cs).
   - Câblage upload/ffp5cs = tranches séparées, chacune sans changement observable.
3. **Brancher `N3NetStats` dans ffp5cs** — appels `n3NetStatsRecord*` depuis
   `web_client.cpp` **impérativement sous `s_httpMutex`** (statique non thread-safe).

### T4 — L5 : orchestration (risque faible/modéré) — ✅ LIVRÉ (T4.1 `n3MailNotify`, T4.2 `n3_ota_ui`, T4.3 `ota_artifact_select`, T4.4 rollback OTA **opt-in inactif** — activation conditionnée à une validation sur cible, cf. `docs/OTA_ROLLBACK_OPT_IN.md`)

1. **`n3MailNotify(project, severity, subject, msg, …)`** → `n3_mail` : extrait
   `sendEmailNotification` (verbatim n3pp/msp sauf préfixe projet, budget
   `FAILOVER_MAIL_BUDGET=8`) ; s'inspirer du **retry persisté RTC** d'upload
   (`pendingOtaFailMail`, `main.cpp:~77-79, 363-389`) pour l'enrichir (option).
2. **`n3_ota_ui`** → `n3_common` : harnais OTA périodique **triplé**
   (n3pp `main.cpp:~30-160`, msp `main.cpp:~146-276`, upload `main.cpp` équivalent).
   `N3OtaPeriodicConfig{title, prodUrl, testUrl, display, version}` ; **encapsuler les
   buffers module-static dans un contexte** ; états `RTC_DATA_ATTR` conservés.
3. **`ota_artifact_select`** (ffp5cs, header pur ArduinoJson) → `n3_ota` : cascade
   `channels[env][model]→…→legacy`, rétro-compatible. **Précondition** : valider la
   parité ArduinoJson v6/v7 sur les 3 toolchains (upload est en 2.0.x).
4. **Rollback OTA 1er boot** (manque identifié par la veille) : image
   `ESP_OTA_IMG_PENDING_VERIFY` + auto-test (WiFi+serveur) +
   `esp_ota_mark_app_valid_cancel_rollback()` ; sinon rollback. Nouvelle capacité →
   opt-in par flag de build, doc dédiée, test sur cible obligatoire avant généralisation.

### T5 — L6 : logging unifié `n3_log` — ✅ LIB LIVRÉE + câblage ffp5cs (migrations `Serial.print` restantes = une PR par firmware)

Extraire de ffp5cs `log.h` une lib `n3_log` **découplée de `config.h`** (niveau via
macro de build), idéalement mappée sur `esp_log`/`CORE_DEBUG_LEVEL`. Migration des
`Serial.print` firmware par firmware (gros volume, faible risque sémantique) — une PR
par firmware, en commençant par uploadphotosserver (plus petit).

> ✅ Livré : `shared/n3_log` 1.0.0 (`n3_log_core.h` pur + `N3_LOG_TAGGED`, échelle
> 0..5 = `CORE_DEBUG_LEVEL`, gates `N3_LOG_LEVEL`/`N3_LOG_ENABLED`, testé natif
> `test_log`) ; ffp5cs 15.19 : `log.h` délègue `_LOG_IMPL` sans changement
> observable. ⏳ Restent les migrations `Serial.print` de n3pp/msp/upload/pgl —
> une PR dédiée par firmware, commencer par uploadphotosserver.

### T6 — L7 : framework `n3_app` (dernier)

Squelette de cycle deep-sleep à callbacks (`onWake, readSensorsOrCapture, buildPayload,
applyRemoteConfig, onSleep`), inspiré ESPHome/Tasmota. **Préalables obligatoires**
(lot 0) : ~~extraire `msp_globals.cpp`~~ ✅ (msp 2.59, extraction verbatim) ;
~~harmoniser A6 (décider `HeureSansWifi` pour msp)~~ ✅ (décision utilisateur 2026-07-14 :
aligner sur n3pp — msp 2.62, `HeureSansWifi()` appelée en échec WiFi),
~~A7 (retirer l'`analogRead` brut msp)~~ ✅ (msp 2.60 : `PontDiv` = moyenne filtrée),
~~A8 (flag anti-spam + lieu de reset + double-site n3pp)~~ ✅ (n3pp 4.59 : bloc
emergency de `sommeil()` = code mort supprimé, POST final + écran rapatriés dans
`automatismes()` ; site unique évaluation + ré-armement par firmware — le lieu
commun définitif se décidera à la conception de `n3_app`),
~~A10 (clamp 102)~~ ✅ (msp 2.61 : clé 102 bornée 0..4095 comme n3pp). Chaque
harmonisation = changement de comportement assumé → tranche dédiée + VERSION.md.

> ✅ **T6.0 (additive) livrée** : `n3_app_seq.h` (`N3AppContext` + `n3AppNextStep`
> pur, `stdint`-only, sans consommateur ni include Arduino/ESP) + `test_app` + CI.
> Suite = wrapper on-target `n3AppRun` (T6.1) puis adoption uploadphotosserver (T6.2).

## 6. Definition of done (par tranche)

- [ ] CI verte (natifs + TOUS les builds matriciels) sur la PR.
- [ ] Aucun changement observable non documenté (câblages) / aucun consommateur (additifs).
- [ ] Tests natifs portés/ajoutés avec assertions à parité, branchés dans la CI.
- [ ] Versions bumpées (libs `library.json` + `shared/README.md` + firmware `VERSION.md`).
- [ ] `docs/PROPOSITION_REFACTORISATION_SHARED.md` annoté (tranche marquée ✅).
- [ ] PR draft ouverte, suivie (subscribe), rouge corrigé avant d'empiler la suite.
