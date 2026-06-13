# Contrat firmware ↔ serveur & vérification de l'audit `ffp5cs`

> **Session bi-dépôt** (firmware `n3_firmwires/ffp5cs` + serveur `n3_serveur`), 2026-06-13.
> Ce document **vérifie** le backlog du handoff d'audit (`AUDIT_FFP5CS_HANDOFF.md`) en
> croisant le code firmware **et** le code serveur, puis fige le **contrat partagé** comme
> référence unique. Plusieurs points du handoff se révèlent **déjà résolus** ou **faux
> positifs** : ils sont corrigés ci-dessous, preuves à l'appui.

---

## 0. Résumé exécutif

| Item handoff | Verdict après vérification croisée | Action |
|---|---|---|
| S1 — Divergence fallback HMAC 401 (`httpRequest` vs `fetchRemoteState`) | **Latent, pas actif** : le GET `outputs/state` n'envoie **aucun** en-tête HMAC et le serveur ne le vérifie pas sur ce chemin. Pas de bug en production. | Documenté (§1). Patch défensif tenu en réserve (build requis). |
| S2 — « lambdas mortes » dans `fillDbVarsJson` | **FAUX POSITIF** : les 3 lambdas (`getStringWithDefault`, `getIntCanonical`, `getFloatCanonical`) sont **toutes appelées**. | Aucune suppression (§2). |
| S2/S4 — schéma `dbvars` & matrice d'endpoints | **Aligné** : 15 champs concordants, 15 endpoints firmware ⇒ 15 routes serveur, 0 orphelin. | Contrat figé (§2, §3). |
| S3 — 3 modes OTA / hébergement | Manifeste **concordant**. Hébergement o2switch : fichiers statiques, `Content-Length` fiable, **pas de Range**. Le mode micro-chunks **n'aide pas** (latence o2switch en amont). | Recommandation d'unification (§4) — build requis. |
| Quick win — `OUTLIER_SPREAD_MM = 150` vs `50` | **FAUX POSITIF** : le `150` était un membre **mort** (jamais référencé) ; le code applique déjà `TankCfg::OUTLIER_SPREAD_MM = 50`. | ✅ Membre mort retiré (`sensors.h`). |
| Quick win — validation capteurs dupliquée 8× | **Confirmé** & sémantiquement équivalent. | ✅ 8 sites migrés vers `SensorValidation::` (`sensors.cpp`, `sensor_air.cpp`). |
| Quick win — `handleMaree()` / `hasSignificantActivity()` | **Déjà retiré** (v11.178). | Aucune action. |
| #4 — état partagé sans verrou (reboots) | **Confirmé & c'est la cause la plus probable des reboots** : `m_otaLock` (bool nu, course inter-cœur) et `s_lastFetchedJson` (vecteur `LoadProhibited`, déjà noté dans le code). | Backlog prioritaire (§5) — build requis. |

**Changements de code appliqués dans cette session** (sans toolchain ESP32, donc strictement
ceux validables par lecture) : voir §6. Tout le reste est **build-gated** et documenté comme
backlog actionnable avec patchs prêts.

---

## 1. S1 — Contrat HMAC / authentification

### Contrat réellement implémenté (identique des deux côtés)

| Élément | Firmware | Serveur |
|---|---|---|
| Algorithme | HMAC-SHA256 | HMAC-SHA256 |
| Clé | `Secrets::API_SIG_SECRET` | secret partagé (`SIG_*`) |
| Message signé | `timestamp + "\n" + nonce + "\n" + body` | `timestamp . "\n" . nonce . "\n" . body` |
| Encodage | hexadécimal minuscule | `hash_hmac('sha256', …)` (hex minuscule) |
| En-têtes | `X-Sig-Timestamp`, `X-Sig-Nonce`, `X-Sig-Hmac` | mêmes en-têtes |
| Fenêtre anti-rejeu | — | 300 s (`SIG_VALID_WINDOW`) |

- Firmware : `src/hmac_sign.cpp:45-91`, application POST dans `src/web_client.cpp:199-216`.
- Serveur : `src/Security/SignatureValidator.php:43-46` (calcul), `src/Controller/Ffp3/PostDataController.php:197-257` (vérif `validateHeaderHmac`).

➡️ **Le schéma de signature concorde parfaitement** sur le chemin POST `post-data*`.

### La « divergence 401 » est latente, pas active

- `fetchRemoteState()` (GET `outputs/state`) **n'ajoute aucun en-tête `X-Sig-*`** avant `_http.GET()`
  (`web_client.cpp` ~ lignes 480-496).
- Côté serveur, `OutputController::getOutputsState()` (`src/Controller/Ffp3/OutputController.php:202-244`)
  **ne fait aucune vérification HMAC** ; la route est enregistrée sans middleware d'auth
  (`config/routes_helpers.php`).
- Donc, **en production aucun 401 HMAC n'est émis sur ce chemin** : le « fallback manquant » ne
  correspond à aucun bug actif.

**Risque résiduel (défensif)** : si un jour le serveur durcit le GET en HMAC strict (comme le
POST), le firmware recevrait un 401 non géré et retomberait silencieusement sur le cache NVS.
Le patch défensif (mirroir du fallback de `httpRequest`) est **tenu en réserve** mais **non
appliqué** ici car (a) non urgent, (b) non compilable dans cette session, (c) code spéculatif
sur un chemin sans bug. À traiter dans une session avec banc + build **si** le durcissement GET
est planifié côté serveur.

---

## 2. S2 — `fillDbVarsJson` : schéma `dbvars` & « lambdas mortes »

### Correction : aucune lambda morte

`src/web_server.cpp` — les trois lambdas sont **vivantes** :

| Lambda | Définition | Appel |
|---|---|---|
| `getStringWithDefault` | ~193-199 | ~247 (`mail`) |
| `getIntCanonical` | ~200-204 | ~236 (`tempsRemplissageSec`, fallback `refillDuration`) |
| `getFloatCanonical` | ~205-209 | ~233 (`chauffageThreshold`, fallback `heaterThreshold`) |

Elles portent la **rétro-compatibilité** des renommages serveur. **Ne pas supprimer.** Le
handoff se trompait sur ce point.

### Schéma `dbvars` canonique (source = serveur)

Source de vérité : `src/Service/OutputSyncService.php:16-45` (mapping GPIO) +
`src/Service/OutputCacheService.php:34-41` (défauts), endpoint `OutputController::getOutputsState`.

| GPIO | Champ canonique | Type | Défaut | Unité |
|---|---|---|---|---|
| 100 | `mail` | string | "" | email |
| 101 | `mailNotif` | int (0/1) | 1 | flag |
| 102 | `aqThreshold` | int | 18 | cm |
| 103 | `tankThreshold` | int | 80 | cm |
| 104 | `chauffageThreshold` | float | 18 | °C |
| 105 | `bouffeMatin` | int | 8 | h (0-23) |
| 106 | `bouffeMidi` | int | 12 | h |
| 107 | `bouffeSoir` | int | 18 | h |
| 108 | `bouffePetits` | int | 1 | flag (reset par ESP32) |
| 109 | `bouffeGros` | int | 0 | flag (reset par ESP32) |
| 110 | `resetMode` | int | 0 | flag |
| 111 | `tempsGros` | int | 3 | s |
| 112 | `tempsPetits` | int | 2 | s |
| 113 | `tempsRemplissageSec` | int | 120 | s (legacy `refillDuration`) |
| 114 | `limFlood` | int | 8 | cm |
| 115 | `WakeUp` | int | 0 | flag |
| 116 | `FreqWakeUp` | int | 600 | s |

Points d'attention (non bloquants, gérés par le firmware) :
- `chauffageThreshold` : stocké int côté serveur, lu float côté firmware (`getFloatCanonical`).
- `mailNotif` : int (0/1) serveur ; le firmware parse aussi `"checked"/"true"/"on"` (robuste).
- `bouffeMatinOk/MidiOk/SoirOk` : **locaux NVS**, hors contrat distant (à ne pas chercher côté serveur).

---

## 3. Matrice d'endpoints (vérifiée)

Firmware `include/server_url_config.h:46-68` ⇄ serveur `config/routes_ffp3.php` +
`config/routes_helpers.php` (`registerFirmwareRoutes`).

5 familles × 3 variantes d'env = 15 chemins, chacun avec sa route serveur (+ alias `/ffp3/*`) :

| Famille | Méthode | Existe serveur |
|---|---|---|
| `post-data` (prod/test/test3/s3/s3-test) | POST → `PostDataController::handle` | ✅ |
| `api/outputs*/state` (5 variantes) | GET → `OutputController::getOutputsState` | ✅ |
| `heartbeat*` (5 variantes) | POST → `HeartbeatController::handle` | ✅ |

➡️ **0 endpoint orphelin** côté firmware ou serveur.

### S4 — dimensionnement buffer `dbvars`

- Réponse compacte (sans `dataStates`) ≈ 700-800 o ; pire cas (avec `dataStates`) ≈ 1000-1300 o.
- Le serveur sérialise **sans `PRETTY_PRINT`** pour rester < 1024 o côté WROOM
  (`OutputController.php` commentaire v4.9.42).
- Buffers firmware (`include/config_buffers.h`) : WROOM 1536 o, S3 2048 o, PSRAM 4096 o.
- **Verdict** : WROOM 1536 o suffit (marge ~20 %, serrée mais sûre). Surveiller la croissance
  de `dataStates`. Ne pas réduire WROOM. PSRAM 4096 o est sur-dimensionné (réductible à 2048).

---

## 4. S3 — OTA & hébergement

### Les 3 modes firmware

| Mode | Fichier | Lib | Range ? | Rôle |
|---|---|---|---|---|
| `downloadFirmwareModern` | `ota_manager_download.cpp:125` | `esp_http_client` | non | 1er choix (perf, fragile TLS) |
| `downloadFirmware` | `ota_manager_download.cpp:488` | `HTTPClient` | non | fallback (retry 5xx) |
| `downloadFirmwareUltraRevolutionary` | `ota_manager_download_alt.cpp:34` | `HTTPClient` | non (chunks 2 Ko applicatifs) | dernier recours |

### Ce que l'hébergement fournit réellement

Serveur `public/index.php` (~232-290) : les `.bin`/`.json` sont servis en **fichiers statiques**
(stream `fopen` 65 Ko), `Content-Length` **fiable** (`filesize()`), **pas de `Accept-Ranges`,
pas de 206**. Manifeste serveur `ota/metadata.json` ⇄ parseur firmware
`ota_manager_validate.cpp` (`selectArtifactFromMetadata`) : champs `version`, `bin_url`, `size`,
`md5` + `channels[env][model]` → **concordants**.

Latence o2switch (`docs/technical/MESSAGE_SUPPORT_O2SWITCH_LATENCE.txt`) : 18-20 s **en amont**
(connexion/proxy), le **streaming une fois connecté est normal**. Donc les micro-chunks
(qui re-lisent le même flux séquentiellement) **n'apportent rien** contre cette latence.

### Recommandation (build requis, structurel)

1. **Unifier** `Modern` → `Classic` en un seul « download adaptatif » ; **retirer** le mode
   micro-chunks (complexité non testée, ralentit sans bénéfice prouvé).
2. Porter le timeout de connexion `Modern` à ~20 s pour absorber la latence o2switch.
3. **Range/resume** : impossible aujourd'hui (hébergeur), à reporter (CDN ou upgrade o2switch).

Non appliqué ici : refactor OTA = banc réel + build + validation des 2 cartes obligatoires.

---

## 5. #4 — Concurrence : cause la plus probable des reboots (backlog prioritaire)

> **Tout ceci nécessite un build** (mutex/atomic/double-buffer = comportement runtime à valider).
> Patchs prêts ci-dessous pour une session avec toolchain.

| Prio | Élément | Écrit / Lu | Course ? | Correctif | Fichiers |
|---|---|---|---|---|---|
| ✅ 1 | `m_otaLock` (**bool nu**) | écrit otaTask (`ota_manager.cpp:292,301,331,350`) / lu netTask (`app_tasks_net.cpp:154`) | **OUI** inter-cœur | **FAIT** : passé en `std::atomic<bool>` (`ota_manager.h`). Compile validée par CI ; observation banc encore recommandée pour confirmer la disparition des reboots. | `ota_manager.h:22` |
| 🔴 2 | `s_lastFetchedJson` | écrit netTask (`web_client.cpp:742-744`) / lu autoTask via `deserializeJson` (`:758`) | **OUI** — `LoadProhibited` **noté dans le code** (`:306,721,801`) | double-buffer ping-pong + swap atomique, **ou** mutex autour write+read, **ou** copie locale en netTask | `web_client.cpp:65-66` |
| 🟠 3 | mutex HTTP `portMAX_DELAY` | `web_client.cpp:116` | inversion de priorité possible si POST 18 s | borner l'attente (`pdMS_TO_TICKS(timeout+5000)`) + échec gracieux | `web_client.cpp:116` |
| 🟠 4 | file `_mailQueue` re-enqueue | `mailer_queue.cpp:86` | rare (retry) | copier `item` sur la pile + valider non-vide avant `xQueueSendToFront` | `mailer_queue.cpp` |

`m_httpClient`/`_http` : **OK** (propriété mono-tâche par sérialisation de file ; pas de course).

### Piles FreeRTOS (réduire la profondeur, ne pas gonfler)

- `autoTask` ~95 % (976 o libres) : déporter les gros `JsonDocument` locaux en statique/BSS,
  sortir `NVS.save()` de la boucle chaude, déplacer `applyRemoteGpioConfig()` vers netTask.
- `webTask` ~88 % (1268 o libres) : pré-sérialiser les réponses fréquentes en BSS.
- `netTask` : `StaticJsonDocument` local volumineux dans la boucle → passer en statique.
- À vérifier (signalé par l'analyse, non confirmé) : `MAIL_TASK_STACK_SIZE` défini sans tâche
  associée (le mail tourne dans `autoTask`) → candidat à suppression de config si confirmé.

---

## 6. Modifications de code appliquées dans cette session

Strictement les changements validables **sans** toolchain ESP32 (équivalence sémantique exacte,
en-têtes déjà inclus via `config.h`) :

1. **`include/sensors.h`** — suppression du membre mort `OUTLIER_SPREAD_MM = 150`
   (jamais référencé ; le code applique `TankCfg::OUTLIER_SPREAD_MM = 50`). Commentaire ajouté
   pour pointer la source de vérité. *(Lève le « bug potentiel » du handoff : c'était un faux positif.)*
2. **`src/sensors.cpp`** (227, 267, 313) — `!isnan && range` → `SensorValidation::isValidWaterTemp()`.
3. **`src/sensor_air.cpp`** (347, 409, 471, 521, 602) — idem vers `isValidAirTemp()` / `isValidHumidity()`.
   Les formes négatives (`isnan || <min || >max`) sont remplacées par `!isValid…()` (De Morgan exact).
4. **`include/ota_manager.h`** — `m_otaLock` : `bool` → `std::atomic<bool>` (race inter-cœur
   sur le verrou OTA, cause probable des reboots). Sites de lecture/écriture inchangés
   (opérateurs transparents). Compile validée par la CI firmware ; **observation banc**
   recommandée pour confirmer la disparition des reboots intermittents.
5. **`test/test_post_body/`** — nouvelle suite Unity native verrouillant le **contrat de corps
   POST HMAC** (`Ffp3PostBody::buildFullUpdateBody` ⇄ serveur `Ffp3HmacPostBody`) : ordre
   canonique des champs, tri alphabétique des extras, omission des vides, gestion buffer trop
   petit. Empêche toute dérive d'ordre/format qui provoquerait des 401 HMAC. Enregistrée dans
   `platformio-native.ini` et la CI (`firmware-ci.yml`). Couvre une des fonctions pures
   prioritaires de l'audit §3.8.
6. **`include/ota_artifact_select.h` + `test/test_ota_select/`** — extraction de la cascade de
   sélection d'artefact OTA (`OTAManager::selectArtifactFromMetadata`) en fonction pure
   `OtaArtifactSelect::selectArtifact` (env/modèle injectés, sans dépendance réseau/mbedtls),
   puis suite native couvrant les 5 branches : `channels[env][model]` → `[env][default]` →
   `[prod][model]` → `[prod][default]` → fallback legacy top-level (+ fallbacks de champs,
   version optionnelle en mode channel vs requise en top-level). Le membre délègue désormais à
   cette fonction (sélection **inchangée** ; logs de debug par-branche consolidés). Verrouille
   le contrat OTA (S3), jusqu'ici sans aucun test. ArduinoJson (header-only) ajouté aux
   `lib_deps` natifs. Valeurs attendues vérifiées par `g++` local + CI.
   > ⚠️ Touche le chemin de sélection OTA : extraction verbatim validée compile par CI + test
   > natif des 5 branches, mais un flash OTA réel sur banc reste recommandé avant prod.

> ⚠️ **Build PlatformIO non disponible dans cette session.** La CI firmware (`pio run`) fait foi
> sur la PR. Aucun `sdkconfig`/partition modifié (seul l'ajout des suites de test à la CI).

---

## 7. Pièges (rappel)

- Ne pas supprimer les `sdkconfig_*.txt` (référencés par `custom_sdkconfig`).
- Ne pas gonfler les piles : réduire la profondeur.
- Tout refactor OTA/HMAC/concurrence ⇒ validation contre le serveur **et** banc réel.
- `managed_components/` : régénérable par le component manager, ne pas re-committer.
