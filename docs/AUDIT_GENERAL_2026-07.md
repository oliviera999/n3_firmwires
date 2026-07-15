# Audit général — n3_firmwires

**Date :** 2026-07-15 · **Branche :** `claude/project-audit-k4byx1`
**Périmètre :** tous les firmwares vivants (`n3pp`, `msp`, `uploadphotosserver`, `ffp5cs`, `poissonglouton`), les libs `shared/`, le build/CI et la cohérence inter-projets.
**Méthode :** lecture directe du code (5 sous-audits parallèles), findings vérifiés `fichier:ligne`. Exclus : `archive/`, `à voir/`, sous-module `ffp5cs/ffp3`.

> Cet audit **remplace** les constats devenus obsolètes de `AUDIT_FIRMWARES_2026.md`, `AUDIT_OPTIMISATION_2026.md`, `RAPPORT_ANALYSE.md`, `RECOMMANDATIONS.md` (versions périmées : ffp5cs 13.92→**15.19**, n3pp 4.39→**4.59**, msp 2.43→**2.62**, upload 2.39→**2.67**, pgl 0.1.2→**0.5.15**). Les libs mortes qu'ils citaient (`n3_http`, `libn3_iot`) **n'existent plus** — seul un commentaire résiduel les mentionne (`shared/n3_common/src/n3_defaults.h:28`).

---

## ✅ Statut d'implémentation (ce PR)

**Corrigés dans ce PR** (bugs de correction + dette sûre) :
- **ffp5cs** (v15.20) : F1 (config web copiée, plus de corruption), F2 (cookie auth `strlen`), F3 (handle OTA remis à `nullptr`), F4 (EMA temp/hum découplées, plus de NAN), F5 (année RTC en `int`), F8 (`loadBool` défaut `!newVal`), F10 (garde null avant déréférencement), F13 (OTA-FS chunked), S3 (auth + anti path-traversal sur `/api/history` & `/api/sd-status`), F14 (instrumentation debug `faa4e5` retirée).
- **n3pp** (v4.60) : N1 (double POST supprimé), N2 (anti-inondation : 1 arrosage/cycle), N3 (serveur web mort retiré), variables mortes.
- **msp** (v2.63) : M1 (serveur web mort retiré), M2 (OLED affiche les valeurs filtrées), M4 (défaut `SeuilSec`), variables mortes.
- **poissonglouton** (v0.5.16) : P1 (comptage du réveil EXT0), P2 (débounce ultrason sur mesure fraîche), P4 (anti re-upload en boucle), `pgl_display_stub.cpp` supprimé, `pgl_audio.cpp`/`pgl_counter.h` reformatés.
- **uploadphotosserver** (v2.68) : U2 (code mort retiré), U1 (commentaire heap).
- **shared** : S5 (fail-closed sur signature absente — no-op runtime, ferme le footgun), S6 (commentaire replay body), commentaire `n3_http`, `n3_wifi` case dupliqué.
- **build/CI** : B1 (BME280 épinglé), B4/B5 (2 suites de tests câblées en CI), B6 (manifest ffp5cs), B7 (`_test` envs héritent des flags), B9 (bandeau), B10 (audits archivés sous `docs/archive/`).

**Volontairement NON traités** (décision utilisateur / coordination requise — voir §7) :
- **S1** (enforcement OTA `N3_OTA_REQUIRE_SIGNATURE`) et **B2** (`DallasTemperature` 3.11.0 vs 4.0.5) : **laissés en l'état** par décision explicite (risque de brique OTA / de régression capteur).
- **S2** (pinning CA TLS) : nécessite les bundles CA + coordination serveur.
- **F6/F7/F11** (races SD/WebSocket/light-sleep ffp5cs) : nécessitent une revue mutex ciblée (risque de deadlock) — différés.
- **P3** (OTA bloquant le comptage pgl), **B3** (versions libs ffp5cs) : différés (refacto / risque de déstabiliser le gros firmware).

> Le détail complet de chaque finding reste ci-dessous. Les items serveur (S1/S2/S6) sont repris en §7 avec un prompt d'analyse bout-en-bout pour **n3_serveur**.

---

## 0. État général du projet

Le dépôt est **globalement sain et en nette progression**. La stratégie de mutualisation `shared/` (OTA, SMTP, WiFi, temps, deep-sleep, ADC, tracker) — chantier ouvert par les audits précédents — est **largement aboutie** : n3pp/msp délèguent désormais à `n3_ota_ui`, `n3_mail`, `n3_time`, `n3_sleep`, `n3_wifi`, `n3_analog_sensors`. Les points forts vérifiés : gestion framebuffer caméra propre (pas de fuite), journal d'événements pgl robuste (CRC16 + ack serveur), HMAC/canonicalisation corrects, tests natifs Unity présents sur `shared/` + ffp5cs.

Les problèmes restants se concentrent sur : **(a)** une posture de sécurité OTA/TLS trop permissive par défaut, **(b)** une poignée de bugs de correction bien réels (surtout ffp5cs, le plus gros = 80 % du code), **(c)** de la dérive de versions de libs dans le build, **(d)** du code mort résiduel et de la duplication n3pp↔msp.

---

## 1. 🔴 Sécurité — à traiter en priorité (transverse à toute la flotte)

### S1 — OTA accepte des binaires NON signés par défaut · HIGH
`shared/n3_common/src/n3_ota.cpp:315-333`. Le garde `N3_OTA_REQUIRE_SIGNATURE` **n'est défini nulle part** (vérifié : 0 occurrence hors du `#if` lui-même). Quand la métadonnée OTA n'inclut pas de `signature`, la mise à jour passe **sur sha256 seul** — or ce sha256 vient du **même** document que l'URL. Un attaquant qui contrôle la métadonnée sert son binaire + son sha256 → flash accepté. Contredit la promesse « sha256 + ECDSA P-256 » du `CLAUDE.md` : l'étape ECDSA est optionnelle et **inerte aujourd'hui**.
→ **Fix :** définir `N3_OTA_REQUIRE_SIGNATURE` dans les `build_flags` de chaque firmware et garantir que la métadonnée serveur porte toujours `signature`.

### S2 — TLS en `setInsecure()` (aucune validation de certificat) · HIGH
`n3_ota.cpp:34-40`, `n3_data/src/n3_data.cpp:40-46`, `ffp5cs/src/web_client.cpp:78`. Le pinning CA est opt-in via `n3_ota_ca_cert.h` / `n3_data_ca_cert.h`, mais **ces fichiers n'existent qu'en `.example`**. Tous les firmwares compilent `USE_HTTPS_ENDPOINTS` mais tombent dans la branche `setInsecure()` → TLS chiffre mais **n'authentifie pas** (tout certificat accepté). C'est ce qui rend S1 exploitable en pratique même en `https://`. Côté ffp5cs, le GET de config distante n'a pas de HMAC → un MITM peut **réécrire la config** renvoyée.
→ **Fix :** fournir les bundles CA et retirer `setInsecure()` en prod, ou assumer explicitement que l'intégrité OTA ne repose actuellement sur rien.

### S3 — `/api/history` et `/api/sd-status` non authentifiés + path traversal · MEDIUM
`ffp5cs/src/web_routes_status.cpp:594,633`. Pas de `webAuthIsAuthenticated`; le param `date` (≤15 c.) est injecté dans `"%s/%s.csv"` (`sd_logger.cpp:192`) sans nettoyage → `date=../../x` sort de `LOG_DIR` → lecture non authentifiée de fichiers arbitraires sur SD.
→ **Fix :** exiger l'auth + valider `date` (whitelist `[0-9-]`, rejeter `..`/`/`).

### S4 — Aucun test sur le chemin de vérification OTA · HIGH (dette de test)
`n3_ota.cpp` : `verifyFirmwareSignature`, le compare sha256 streaming, le magic `0xE9`, les gardes de taille/partition ne sont **couverts par aucun test natif** (les tests existent pour rollback/artifact-select/periodic, pas pour le verify). Le code le plus exposé du dépôt n'a pas de filet.

### S5 — `verifyFirmwareSignature` renvoie `true` si la signature est vide · MEDIUM
`n3_ota.cpp:54-55` : `if (... || signatureB64[0]=='\0') return true;`. Une fonction de vérif qui renvoie « OK » pour une signature absente est un fail-open ; masqué aujourd'hui par le garde appelant, mais piège pour tout futur appelant.

### S6 — Signature « legacy » du body ne couvre que le timestamp · MEDIUM
`n3_data/src/n3_data.cpp:88-100` : le HMAC ne signe que `tsBuf` (epoch), pas le payload → une paire `timestamp/signature` capturée est **rejouable avec un body arbitraire** dans la fenêtre serveur. Les en-têtes `X-Sig-*` (125-141) couvrent bien le body ; retirer le champ legacy du contrat accepté côté serveur.

---

## 2. 🐛 Bugs de correction (par firmware)

### ffp5cs (les plus impactants — vérifiés en lecture directe)

| # | Sévérité | Fichier:ligne | Bug |
|---|----------|---------------|-----|
| F1 | **HIGH** | `web_server.cpp:772` | `nvsDoc[key] = value` où `value` est un `const char*` pointant sur `paramBuf` réutilisé. ArduinoJson v6 stocke le `const char*` **par référence** → toutes les clés pointent le même buffer → à la sérialisation, **chaque champ prend la valeur du dernier param**. Toute sauvegarde de config web en NVS est corrompue. **Vérifié.** Fix : forcer une copie (`.set<String>(value)` ou buffer stable par clé). |
| F2 | **HIGH** | `web_server.cpp:112` | `prefixLen = 11` alors que `"ffp5cs_auth="` fait **12** caractères → la comparaison de token démarre sur le `=` → l'auth par cookie échoue toujours (fail-closed). **Vérifié.** Fix : `prefixLen = 12` / `strlen(prefix)`. |
| F3 | **HIGH** | `ota_manager_download.cpp:760` + `ota_manager.cpp:347` | `updateTask()` fait `vTaskDelete(NULL)` sans remettre `m_updateTaskHandle=nullptr` sur l'échec → `performUpdate()` voit un handle non-nul et renvoie « déjà en cours » **pour toujours**. Un seul download OTA raté désactive tous les suivants jusqu'au reboot. Fix : nuller le handle avant `vTaskDelete`. |
| F4 | **HIGH** | `sensor_air.cpp:474/488/524` | `filteredTemperatureC()` et `filteredHumidity()` partagent **un seul** `_emaInit` (et `_lastDhtReadMs`). Le 2e appelé ne s'initialise jamais → humidité = `0.3*h + 0.7*NAN = NAN` en permanence, empoisonne `_lastValidHumidity`. Fix : flags/timestamps séparés temp/hum. |
| F5 | **HIGH** | `rtc_ds3231.cpp:163` | `uint8_t year = t.tm_year + 1900;` → 2026 tronqué à **234** → `year>=2000` toujours faux → écriture RTC corrompue, l'heure DS3231 ne persiste pas au deep sleep. **Vérifié.** Fix : `int`/`uint16_t`. |
| F6 | MEDIUM | `web_server.cpp:171,369,711` | Course inter-tâches sur `s_dbvarsCachedSrc`/`StaticJsonDocument` (handler HTTP async + callback WebSocket temps réel, sans lock) → corruption/crash possible. |
| F7 | MEDIUM | `sd_logger.cpp:98` | `SD.open/printf/close` sans mutex, atteint depuis autoTask **et** webTask → FATFS non réentrant → corruption log/queue. |
| F8 | MEDIUM | `gpio_parser.cpp:619` | `loadBool(..., currentVal, newVal)` avec `newVal` en défaut : clé absente ⇒ `currentVal==newVal` ⇒ le garde `if(currentVal!=newVal)` bloque la **première** écriture → états actionneurs jamais persistés. |
| F9 | MEDIUM | `gpio_parser.cpp:325/341/436` | Garde d'overflow `memoryUsage() < 1200` sur un `StaticJsonDocument<1024>` → toujours vrai, garde mort ; à saturation la clé est droppée silencieusement. |
| F10 | MEDIUM | `nvs_manager_typed.cpp:66-80` | `loadString` déréférence `value`/`valueSize` **avant** le garde null/zéro → `valueSize==0` ⇒ `valueSize-1` wrap `SIZE_MAX` (copie non bornée), `value==nullptr` ⇒ null-deref. |
| F11 | MEDIUM | `power.cpp:80-156` | `goToLightSleep()` ne prend pas `TLSMutex` → sommeil possible en plein handshake TLS/SMTP → connexion corrompue, risque WDT au réveil. |
| F12 | MEDIUM | `nvs_manager_typed.cpp:37`, `nvs_manager.cpp:419/438` | Buffers **4 Ko sur la pile** (`JSON_DOCUMENT_SIZE`) dans des tâches à pile 3 Ko → risque d'overflow. Préférer le heap / une taille bornée. |
| F13 | LOW | `ota_manager_download_alt.cpp:96,144` | OTA FS « chunked » cassé : `int contentLength = getSize()` = `-1` comparé à un `size_t` → ~4 Go → rejet « trop grand » ; partition effacée avant download, sans MD5 ni rollback. |
| F14 | LOW | — | Instrumentation debug de prod : blocs `#region agent log` émettant `"sessionId":"faa4e5"` sur ~45 sites (`rtc_ds3231.cpp`, `power.cpp`, `i2c_bus.cpp`…). Bruit série + coût flash. À retirer. |

### n3pp

| # | Sévérité | Fichier:ligne | Bug |
|---|----------|---------------|-----|
| N1 | **HIGH** | `main.cpp:217-221` + `n3pp_automation.cpp:322` | `static bool firstLoop` en `.bss` → **réinitialisé à `true` à chaque réveil** (design deep-sleep) → `datatobdd()` (220) puis `datatobdd()` du `sommeil()` (322) → **2 POST HTTPS complets à chaque cycle**. **Vérifié.** Double airtime/conso/charge serveur. Fix : supprimer le POST « premier tour » (le POST de `sommeil` suffit). |
| N2 | **HIGH** | `n3pp_automation.cpp:235-298` | Trois branches d'arrosage (sol sec cooldown, heure programmée, manuel) **sans exclusion mutuelle** → à l'heure programmée + sol sec + cooldown expiré, la branche « sol sec » arrose 20 s **et** remet le cooldown à 0 (l.106), puis la branche « heure » arrose encore 20 s → **sur-arrosage / risque d'inondation** + 2-3 POST en trop. Fix : garde « déjà arrosé ce cycle ». |
| N3 | MEDIUM | `n3pp_globals.cpp:142,144` | `AsyncWebServer server(80)` + `WiFiUDP wifiUdp` instanciés mais **jamais** `begin()/on()/addHandler()` (vérifié) → serveur web mort + includes `AsyncTCP`/`ESPAsyncWebServer` pour rien. Retirer (gain flash/RAM). |
| N4 | LOW | `n3pp_sensors.cpp:96-100,144` | `pontdiv` lu 2×/cycle (filtré dans `lectureCapteurs` puis re-échantillonné par `n3BatteryRead`) → ADC redondant + 2 valeurs batterie divergentes. |

### msp

| # | Sévérité | Fichier:ligne | Bug |
|---|----------|---------------|-----|
| M1 | MEDIUM | `main.cpp:32` | `AsyncWebServer server(80)` mort (idem N3). |
| M2 | MEDIUM | `msp_display.cpp:47,49` | `affichageOLED()` fait `analogRead(HumiditeSol)`/`analogRead(PLUIE)` **bruts** au lieu des globales filtrées (dont le sentinelle pluie-déconnectée) → l'OLED affiche d'autres valeurs que celles POSTées. |
| M3 | LOW | `msp_sensors.cpp` (348-421, 446-473) + `msp_display.cpp` | Fenêtre bloquante ~700 ms (`delay(100)`×7 + DS18B20) + OLED 5×500 ms → prolonge le temps éveillé (coût énergie #1 sur nœud solaire). Trimmer les délais inter-lectures et le dwell OLED. |
| M4 | LOW | `msp_globals.cpp:69` | `SeuilSec` défaut = **5000**, hors plage ADC 0-4095 (inerte car msp n'agit pas dessus, mais piège). |

### poissonglouton

| # | Sévérité | Fichier:ligne | Bug |
|---|----------|---------------|-----|
| P1 | **HIGH** | `pgl_detection.cpp:51,158-166` + `main.cpp:508` | Réveil deep-sleep EXT0 : `begin()` lit `irPrevState_` alors que la pin est **déjà LOW** (l'obstacle qui a réveillé) → pas de front HIGH→LOW → **la bouteille qui réveille n'est jamais comptée**. Chaque détection en mode sleep est perdue. (Gated `PGL_ENABLE_SLEEP`, off par défaut, mais défait le but du compteur en conf batterie.) Fix : « réveil EXT0 ⇒ +1 ». |
| P2 | **HIGH** | `pgl_detection.cpp:93-100,168-183` | Le filtre « N polls consécutifs » ultrason est défait par le cache de lecture (100 ms) : `poll()` (~10 ms) réinjecte la **même** mesure → `usBelowCount_` monte ~10×/mesure → `PGL_US_CONSECUTIVE_POLLS=2` satisfait par **une seule** mesure → quasi aucune réjection de bruit → sur-comptage. |
| P3 | MEDIUM | `main.cpp:181-213,604-720` | `n3OtaCheck()` synchrone dans `loop()` → pendant le download (plusieurs s), `gDetection.poll()` non appelé → bouteilles ratées. |
| P4 | MEDIUM | `main.cpp:403-440`, `pgl_counter.cpp:251-279` | Si le serveur ack un id hors batch, `commitJournalAck()` n'avance rien → la boucle `while(pending>0)` **ré-uploade le même batch** jusqu'à épuisement du budget 8 s → POST dupliqués (repose sur la dédup serveur par `eventId`). |

### uploadphotosserver

| # | Sévérité | Fichier:ligne | Bug |
|---|----------|---------------|-----|
| U1 | MEDIUM | `config.h:22-33`, `main.cpp:19-24` | Uploads HTTPS (TLS) **on par défaut** sur ESP32-CAM : sur modules sans PSRAM détectée (`camera_setup.cpp:148-152`), session TLS + framebuffer en DRAM se disputent le heap interne → risque OOM/panic. Le code défère déjà le mail OTA en RTC pour éviter les panics TLS-sur-loopTask ; même pression sur l'upload. Valider le headroom heap par cible. |
| U2 | LOW | `camera_sync.cpp:256-269` | `cameraSyncNextPictureNumber()` / `cameraSyncWrittenCount()` non référencés (code mort) ; le premier garde l'ancien incrément NVS « brûleur de numéro ». Retirer. |

---

## 3. 🏗️ Build / CI / cohérence

| # | Sévérité | Où | Constat |
|---|----------|-----|---------|
| B1 | **HIGH** | `ffp5cs/platformio.ini` `[env:wroom-s3-base]` | `Adafruit BME280 Library` **non épinglé** (seule dépendance flottante du dépôt) → builds S3 non reproductibles. Pinner (ex. `@2.2.4`). |
| B2 | **HIGH** | `msp` vs `ffp5cs` | **Dérive majeure `DallasTemperature`** : msp `@4.0.5` vs ffp5cs `@3.11.0` (breaking 3.x→4.x), même capteur, comportements runtime divergents. |
| B3 | MEDIUM | ffp5cs `[common]` | Retard systématique des libs partagées vs n3pp/msp/upload : `ArduinoJson` 7.4.2 vs 7.4.3, `ESP32Time` 2.0.0 vs 2.0.6, `ESPAsyncWebServer` 3.7.6 vs 3.10.0, GFX/SSD1306/DHT/Unified-Sensor… → le **même** code `shared/` est validé contre des versions différentes selon le firmware. Consolider un set épinglé unique. |
| B4 | MEDIUM | `firmware-ci.yml` | Suite native `test_unit_convert` (présente + dans `platformio-native.ini`) **jamais lancée en CI** (boucle ffp5cs hardcodée l'omet). Angle mort. |
| B5 | MEDIUM | `ffp5cs/test/test_shared_alert_gate/` | Suite **orpheline** : absente de `platformio-native.ini` **et** de la CI → ne s'exécute nulle part. |
| B6 | MEDIUM | `firmwares.manifest.json` | pioEnvs ffp5cs obsolètes : seuls `wroom-prod/test` listés alors que la CI build ~14 envs et que **`wroom-s3-prod`** (cible prod S3 réelle) manque au « catalogue machine ». |
| B7 | MEDIUM | CI vs manifest | Les envs `esp32dev_test` (n3pp, msp) déclarés au manifest ne sont **jamais** buildés en CI, et **redéfinissent** `build_flags` en bloc (perdent `-DUSE_HTTPS_ENDPOINTS`) au lieu d'étendre le parent → set HTTP-only non testé qui peut pourrir. |
| B8 | MEDIUM | `ffp5cs/VERSION.md:11` | Doc pointe `include/config.h (ProjectConfig::VERSION)` ; le define est en `include/config_system.h:13`. Un bumper suivant VERSION.md édite le mauvais fichier. |
| B9 | LOW | `ffp5cs/platformio.ini:12` | Bandeau « firmware courant v13.92 » alors que réel = **15.19**. |
| B10 | LOW | racine | Sprawl de rapports d'audit à la racine (`AUDIT_*`, `RAPPORT_ANALYSE`, `RECOMMANDATIONS`) → déplacer sous `docs/` (fait pour celui-ci). |

---

## 4. 🧹 Code mort / duplication / cleanup

- **Serveurs web morts** : `AsyncWebServer` instancié sans usage dans n3pp **et** msp (N3/M1) + `WiFiUDP` n3pp. Retrait = gain flash/RAM + suppression des deps `AsyncTCP`/`ESPAsyncWebServer`.
- **Duplication n3pp↔msp** (les deux firmwares se ressemblent, pas encore mutualisés) :
  - `n3ppMaybeSendNetworkReportEmail()` (`n3pp_network.cpp:303-386`) ≈ `mspMaybeSendNetworkReportEmail()` (`msp_automation.cpp:82-165`), ~80 lignes quasi-identiques → à plier dans `shared/n3_mail`.
  - Parseur de clés config distantes `variablestoesp()` (`n3pp_network.cpp:179-244` ↔ `msp_network.cpp:178-327`) — dupliqué, la CLAUDE-todo msp l.271-275 le flag déjà.
  - `postPreview` : ~25 concaténations `String` reconstruisant tout le payload juste pour le log (`n3pp_network.cpp:62-87`, `msp_network.cpp:64-90`) → itérer sur `fields[]`.
- **ffp5cs garde sa propre pile OTA ~1650 lignes** (`ota_manager*.cpp`) au lieu de `shared/n3_ota` → tout durcissement du shared OTA (S1/S5) **ne l'atteint pas**. Plus gros chevauchement du dépôt.
- **Variables mortes** : `WakeUpButton`, `batt`, `sensorLocation` (n3pp+msp), `Oled`, `emailHumidSent` (msp) — définies/externées, jamais lues.
- **Fichiers/format morts pgl** : `pgl_display_stub.cpp` (vide, compilé partout), `pgl_audio.cpp`/`pgl_counter.h` en **double-interligne** (~2× la taille, diffs bruités).
- **Commentaire résiduel** `n3_http` (`n3_defaults.h:28`), doublon de `case` FSM (`n3_wifi.cpp:423-439`), 104 marqueurs `TODO/FIXME` en code vivant.

---

## 5. 🎨 UI / UX

- **OLED (n3pp/msp) sans retour d'erreur** : l'état hors-ligne / échec POST n'apparaît que sur la série ; l'écran défile SSID/IP/capteurs quel que soit le résultat du dernier POST. Un indicateur « last POST: OK/ERR » sur la page principale aiderait beaucoup au diagnostic terrain. Pages bloquantes 500-800 ms sans navigation à la demande.
- **% batterie trompeur** : constantes magiques (`2100`/`0.2`/`4.2`) décorrélées du vrai `SeuilPontDiv` (1700) qui déclenche le sommeil basse batterie → l'affichage « 0 % » ne coïncide pas avec la protection. Centraliser dans `n3_battery`.
- **ffp5cs web** : au-delà des bugs F1/F2 (config non sauvée + cookie auth cassé) qui plombent l'UX config, le point le plus visible pour l'utilisateur.

---

## 6. Plan d'action recommandé (priorisé)

**Sprint 1 — sécurité & bugs bloquants (½–1 j)**
1. S1+S2 : définir `N3_OTA_REQUIRE_SIGNATURE` + fournir les bundles CA (ou décision explicite documentée).
2. F1, F2 (ffp5cs : config web + cookie auth), F3 (OTA self-lock), F5 (RTC year).
3. N1 (double-POST n3pp), N2 (sur-arrosage).
4. S3 (auth + sanitize `/api/history`).

**Sprint 2 — robustesse & data (1–2 j)**
5. F4 (humidité NAN), F6/F7 (races SD/WS ffp5cs), P1/P2 (comptage pgl), F8/F9/F10.
6. B1/B2/B3 (pinner/unifier les libs), B4/B5 (câbler les 2 suites de test), S4 (test du verify OTA).

**Sprint 3 — dette & cohérence (au fil de l'eau)**
7. Retrait serveurs web morts (N3/M1) + variables/fichiers morts.
8. Mutualiser duplication n3pp↔msp (mail réseau, parseur clés) dans `shared/`.
9. B6/B7/B8/B9 (manifest, VERSION.md, bandeaux), retrait instrumentation debug F14.
10. UI OLED (indicateur POST OK/ERR), % batterie cohérent.

> Chaque fix touchant un firmware doit **bumper sa version** (`include/*config.h` / `config_system.h` / `VERSION.md`) et suivre la règle anti-conflit inter-PR du `CLAUDE.md`.

---

## 7. Suite côté serveur (`n3_serveur`) — bout-en-bout

Plusieurs findings ne se referment **que côté serveur** ou exigent une vérification bout-en-bout. À traiter dans le dépôt **n3_serveur** (le firmware est ici, le serveur reçoit/vérifie/déploie) :

1. **Contrat de signature OTA (S1)** — avant d'activer `N3_OTA_REQUIRE_SIGNATURE` sur la flotte, **garantir que le serveur signe TOUTE métadonnée OTA** (champ `signature` ECDSA P-256 présent pour chaque cible : n3pp, msp, cam-*, pgl, ffp5-wroom). Séquence sûre : (a) serveur signe systématiquement → (b) vérifier sur banc qu'un device valide bien la signature → (c) **alors seulement** activer le flag firmware. Activer avant (a) **brique l'OTA**.
2. **TLS / pinning CA (S2)** — fournir les bundles `n3_ota_ca_cert.h` / `n3_data_ca_cert.h` (chaîne du domaine `iot.olution.info`) et retirer `setInsecure()` en prod. Vérifier que le certificat serveur est stable / que la rotation est documentée.
3. **HMAC anti-rejeu (S6)** — côté serveur : **cesser d'accepter** la paire legacy `timestamp=&signature=` (qui ne signe que l'epoch, rejouable) une fois que tous les firmwares émettent `X-Sig-*` (corps complet). Vérifier la fenêtre de tolérance temporelle et la dédup par nonce.
4. **Path-traversal / auth (S3)** — le fix firmware bloque `date` malformé côté ESP, mais vérifier que le **serveur** ne construit pas non plus de chemins à partir d'entrées client sans validation (défense en profondeur).
5. **Cohérence des versions** — après ce PR, les cibles OTA côté serveur doivent référencer les nouvelles versions (n3pp 4.60, msp 2.63, upload 2.68, pgl 0.5.16, ffp5cs 15.20).

### 📋 Prompt prêt à l'emploi — analyse serveur bout-en-bout

> À coller dans une session Claude Code ouverte sur le dépôt **n3_serveur** (après y avoir donné accès). Il vérifie que la chaîne firmware → serveur est cohérente de bout en bout.

```
Audit bout-en-bout du contrat firmware ↔ serveur pour l'écosystème n3 IoT.
Contexte : les firmwares vivent dans le dépôt n3_firmwires ; ce dépôt (n3_serveur)
reçoit les POST de données, sert la config distante, et distribue les OTA. Un audit
firmware récent (docs/AUDIT_GENERAL_2026-07.md côté n3_firmwires) a relevé des points
de sécurité qui ne se referment que côté serveur. Analyse le code serveur et réponds,
preuve à l'appui (fichier:ligne), à CHAQUE point :

1. OTA — SIGNATURE : le serveur génère-t-il et inclut-il TOUJOURS un champ `signature`
   (ECDSA P-256 sur le sha256 du binaire) dans la métadonnée OTA de CHAQUE cible
   (n3pp, msp, cam, pgl, ffp3/ffp5-wroom) ? Où est signée la métadonnée ? La clé privée
   correspond-elle à la clé publique embarquée côté firmware (n3_common/n3_ota) ?
   → Objectif : pouvoir activer `N3_OTA_REQUIRE_SIGNATURE` sur la flotte sans casser l'OTA.

2. OTA — INTÉGRITÉ : le sha256 servi dans la métadonnée est-il calculé sur le binaire
   réellement distribué ? Le endpoint OTA est-il servi en HTTPS avec un certificat valide
   et stable (pour permettre le pinning CA côté firmware) ?

3. POST DONNÉES — HMAC : le serveur vérifie-t-il l'en-tête `X-Signature` / `X-Sig-*`
   (HMAC-SHA256 du CORPS complet) ? Accepte-t-il ENCORE la paire legacy
   `timestamp=&signature=` (qui ne signe que l'epoch, donc rejouable avec un corps
   arbitraire) ? Quelle est la fenêtre de tolérance temporelle et la dédup anti-rejeu
   (par nonce) ? → Objectif : retirer le chemin legacy en toute sécurité.

4. CONFIG DISTANTE (GET) : la config renvoyée aux firmwares (clés 100..112 côté n3pp/msp,
   remoteVars côté ffp5cs) est-elle signée/authentifiée, ou un MITM peut-il la réécrire ?
   Les plages de valeurs sont-elles validées côté serveur aussi ?

5. CONTRAT DE CHAMPS : compare les champs POST attendus par le serveur avec ceux émis par
   les firmwares (sensor=, BOARD_TYPE, post_id, etc. — cf. NOMENCLATURE_FFP3). Y a-t-il des
   champs orphelins (émis mais ignorés) ou requis mais non émis ?

6. VERSIONS OTA : les cibles OTA référencent-elles bien les dernières versions firmware
   (n3pp 4.60, msp 2.63, upload 2.68, pgl 0.5.16, ffp5cs 15.20) ?

7. PATH TRAVERSAL / injection : le serveur construit-il des chemins fichiers ou des requêtes
   SQL à partir d'entrées client (date, sensor, id…) sans validation ?

Livrable : un rapport priorisé (CRITICAL→LOW) avec, pour chaque écart firmware↔serveur,
le fichier:ligne serveur concerné et le correctif proposé. Termine par la séquence de
déploiement SÛRE pour activer l'enforcement de signature OTA sans briquer la flotte.
```
