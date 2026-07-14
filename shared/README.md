# `firmwires/shared/` — Bibliothèques communes IoT n3

Bibliothèques PlatformIO utilisées par les firmwares ESP32 du projet IoT n3 (n3pp, msp, uploadphotosserver, et — pour certaines — ffp5cs).

Chaque dossier contient :

- `library.json` : nom, version, dépendances PlatformIO.
- `src/` : sources C++ Arduino (`.h` + `.cpp`).

## Tests natifs

Tests unitaires hôte (Unity) pour la logique pure, sous [`tests_native/`](tests_native/) :

```bash
cd shared/tests_native
pio test -c platformio-native.ini -e native
```

Chaque test inclut directement le `.cpp` testé avec un mock Arduino minimal
(`mocks/Arduino.h`, `analogRead` injectable) ou des stubs (`stubs/` : mbedtls,
HTTPClient) — aucune dépendance matérielle. Couverture actuelle :
- `n3_analog_sensors` (`test_analog`) : médiane, rejet d'outliers, moyenne, EMA, fallback, tensions batterie.
- `n3_hmac` (`test_hmac`) : contrat du wrapper plat (formatage hex 64 car., garde-fou de taille, header `X-Signature`). `test_hmac_canonical` : contrat de `n3_hmac_canonical` (format hex, garde-fous de paramètres, format du nonce). La justesse crypto et l'entropie du nonce restent validées sur cible (vrai mbedtls + HW RNG).
- `n3_tracker` (`test_tracker`) : suivi de pics de balayage, fusion pondérée avec validation, fenêtre fine, asservissement différentiel (zone morte, anti-oscillation, butée, divergence), gains d'égalisation LDR (calibration).
- Logique pure mutualisée depuis ffp5cs (mêmes assertions que les suites ffp5cs d'origine, parité octet-identique) : `test_epoch_util`, `test_clock_decision`, `test_uptime_format` (lib `n3_time`) ; `test_sleep_decision`, `test_reset_reason`, `test_login_throttle` (lib `n3_common`) ; `test_sensor_fallback` (lib `n3_analog_sensors`, API renommée neutre).
- `n3_store_forward` (`test_store_forward`) : invariants du drain offline (peek→commit jamais burn, hybride/plafonds, pacing au complément, budget temps = report ≠ échec, retries 429, HardFail=skip) — temps/attente injectés.
- `n3_upload` (`test_upload_multipart`) : lecteur multipart streamé (séquence head|corps|tail octet-exacte, chunking aux frontières, source au compte-goutte/tronquée, rewind pour retry).
- `n3_sensor_failure_manager` (`test_sensor_failure`) : machine d'état de défaillance capteur (désactivation après N échecs, cadence de réactivation via `millis()` injectable, réactivation après M succès) — logique auparavant non testée.
- Harnais OTA périodique (`test_ota_ui`) : cadence 2 h persistée RTC (`OtaPeriodic`, lib `n3_common` — due/restant/cumul saturant sans débordement) + logique d'écran (`n3_ota_ui_logic.h`, lib `n3_ota_ui` : détection « déjà à jour », pourcentage de fin, barre de progression) — logique auparavant non testée.
- Sélection d'artefact OTA (`test_ota_select`, lib `n3_common`) : cascade `channels[env][model] → [env][default] → [prod][model] → [prod][default] → legacy top-level`, image filesystem, champs d'intégrité sha256/signature — assertions à parité avec la suite ffp5cs d'origine (+ tests `readIntegrityFields`).
- Rollback OTA 1er boot (`test_ota_rollback`, lib `n3_common`, capacité opt-in) : décision pure `OtaRollback::decide` — image non pending inerte, auto-test OK valide même après la fenêtre, échec persistant à expiration = rollback.

Note : `pio test` est lancé **par suite** (`-f`) car le runner natif multi-suites de PlatformIO échoue à enchaîner plusieurs binaires de test (voir la CI `.github/workflows/firmware-ci.yml`). À étendre aux autres libs à logique pure.

## Inventaire

| Lib | Version | Description courte | Dépendances |
|-----|---------|--------------------|-------------|
| [`n3_analog_sensors`](n3_analog_sensors/) | 1.1.0 | Lecture ADC filtrée (médiane + outliers + EMA). Robustesse inter-lectures mutualisée depuis ffp5cs : `n3_sensor_failure_manager` (désactivation/réactivation auto d'un capteur défaillant, macro de log injectable `N3_SENSOR_LOG_PRINTF`), `n3_sensor_fallback` (cascade `current → dernier bon → défaut`, format POST). Utilisée par n3pp, msp, ffp5cs. | — |
| [`n3_battery`](n3_battery/) | 1.0.1 | Pont diviseur (délègue à `n3_analog_sensors`). | `n3_analog_sensors ^1.0.0` |
| [`n3_wifi`](n3_wifi/) | 1.1.0 | Connexion WiFi multi-réseaux avec scan+RSSI+BSSID, callbacks. | — |
| [`n3_http`](n3_http/) | 1.1.0 | **Déprécié** : GET/POST minimal HTTPClient avec timeout (`N3_HTTP_TIMEOUT_MS`). Préférer `n3_data`. | `n3_common ^1.3.0` |
| [`n3_data`](n3_data/) | 1.3.0 | POST/GET URL-encoded, HMAC (X-Sig-* via `n3_hmac_canonical`, sans String), timeout 5 s. Heartbeat générique `n3DataSendHeartbeat` (mutualisé n3pp/msp). Logs `[SERVER][POST/GET] Verdict` + stats `N3NetStatsSnapshot` pour rapports mail. | `n3_hmac`, `n3_common` |
| [`n3_hmac`](n3_hmac/) | 1.1.0 | HMAC-SHA256 via mbedtls + helper d'attache du header `X-Signature`. `n3_hmac_canonical` (mutualisé ffp5cs) : HMAC canonique `ts+"\n"+nonce+"\n"+body` **sans `String`** (chemin chaud) + `generateNonce`, pour les en-têtes `X-Sig-*`. | — |
| [`n3_mail`](n3_mail/) | 1.4.0 | Envoi SMTP, debug body, rapport réseau périodique (`n3MailBuildNetReportBody`), taxonomie `n3_notify` (P1-P4/modes) et **notification graduée avec failover** `n3MailNotify` (mutualisé n3pp/msp : cap P1/P2 hors-ligne, garde WiFi, budget borné). | `n3_data`, `mobizt/ESP Mail Client` |
| [`n3_time`](n3_time/) | 1.2.0 | Sauvegarde/restauration heure RTC en flash NVS, raison de réveil, resync calendaire `n3TimeSyncBrokenDown` (mutualisé n3pp/msp). Logique pure mutualisée (depuis ffp5cs) : `n3_epoch_util` (validation epoch anti-overflow 32-bit), `n3_clock_decision` (plausibilité NTP + dérive PPM), `n3_uptime_format`. | `fbiego/ESP32Time` |
| [`n3_common`](n3_common/) | 1.8.0 | OTA HTTP distant avec vérif sha256 + ECDSA P-256 (`n3_ota`), constantes `n3_defaults.h`, parsing JSON outputs `n3_outputs_json` (factorisation 2026-05), cadence OTA périodique pure `n3_ota_periodic` (`OtaPeriodic` : due/restant/cumul saturant, mutualisé n3pp/msp/upload), sélection d'artefact OTA multi-cible `n3_ota_artifact_select` (`OtaArtifactSelect`, mutualisé ffp5cs : cascade `channels[env][model]→…→legacy`, image filesystem, champs d'intégrité sha256/signature), rollback OTA 1er boot **opt-in** `n3_ota_rollback` (inerte sans `-DN3_OTA_ROLLBACK_ENABLE`, cf. `docs/OTA_ROLLBACK_OPT_IN.md`). Logique pure mutualisée (depuis ffp5cs) : `n3_sleep_decision` (délai de sommeil adaptatif), `n3_reset_reason` (libellé + classification crash), `n3_login_throttle` (anti-brute-force). | `bblanchon/ArduinoJson ^7.4.3`, `arduino-libraries/Arduino_JSON ^0.2.0` |
| [`n3_sleep`](n3_sleep/) | 1.0.0 | Configuration et démarrage du deep sleep ESP32 (timer + GPIO ext0). | — |
| [`n3_ota_ui`](n3_ota_ui/) | 1.0.0 | Harnais OTA périodique + écran OLED (mutualisé n3pp/msp) : `N3OtaUiContext` (buffers d'affichage encapsulés), check immédiat avant reset distant, check périodique 2 h (compteur `RTC_DATA_ATTR` possédé par le firmware), écran de progression SSD1306. Cadence déléguée à `n3_common/n3_ota_periodic`. Logique pure testée en natif (`test_ota_ui`). | `n3_common`, `adafruit/Adafruit SSD1306` |
| [`n3_display`](n3_display/) | 1.0.1 | Init OLED SSD1306 (sondage I2C + retry). | `adafruit/Adafruit SSD1306` |
| [`n3_store_forward`](n3_store_forward/) | 1.0.0 | File offline durable (store-and-forward) : orchestrateur de drain **pur** (`n3SfDrain` — peek→commit jamais burn, stratégie hybride, pacing, budget temps, retries 429) sur backend abstrait (SD/NVS fournis par le firmware). Généralisé depuis uploadphotosserver (`camera_sync`) et ffp5cs (`web_client_queue`). Testé en natif (`test_store_forward`). | — |
| [`n3_upload`](n3_upload/) | 1.0.0 | Upload de gros binaires : `n3UploadMultipart` — POST HTTP multipart **streamé sans malloc du corps** (TLS opt-in + pinning CA comme `n3_data`, retries, en-têtes dynamiques par tentative via `onBeforeSend`, hook `onStats` → `n3NetStatsRecordPost`) sur `N3UploadSource` abstraite + `N3MultipartReader`. Mutualisé depuis uploadphotosserver. Logique pure testée en natif (`test_upload_multipart`). | — |
| [`n3_tracker`](n3_tracker/) | 1.0.0 | Logique pure du tracker solaire msp (asservissement différentiel 2 LDR/axe, pics de balayage, fusion pondérée). Sans dépendance Arduino, testée en natif (`test_tracker`). | — |

## Intégration

Chaque firmware référence `shared/` via `lib_extra_dirs = ../shared` dans son `platformio.ini` (n3pp, msp, uploadphotosserver, ffp5cs).

Les bornes physiques (DHT -40..80 °C, 0..100 %), les fallbacks (DHT 20 °C / 50 %, DS18B20 20 °C), et les sentinelles de capteurs déconnectés sont **dans les firmwares** (pas dans les libs) car les seuils peuvent varier selon le matériel. Les libs fournissent les briques (filtrage, détection isnan, retours codés) et laissent la décision au firmware.

## Sécurité

- **OTA** : `n3_common/n3_ota` vérifie le sha256 du firmware téléchargé puis la signature ECDSA P-256 si le champ `signature` est présent dans `metadata.json`. Cf. [`serveur/docs/OTA_N3PP_MSP.md`](../../serveur/docs/OTA_N3PP_MSP.md).
- **POST** : `n3_data` peut signer le timestamp avec `API_SIG_SECRET` (alignement contrat FFP3, serveur valide via `HmacAuthTrait`). Cf. [`serveur/docs/API_MSP1_N3PP.md`](../../serveur/docs/API_MSP1_N3PP.md).
- **Secrets** : tous les credentials sont dans `firmwires/credentials.h` (non versionné). Modèle : `firmwires/credentials.h.example`. Clé OTA privée : sous `scripts/ota_keys/` (gitignored).

## Versionnage

Toute modification d'une lib doit incrémenter sa `version` dans `library.json` (semver). Aligner aussi `firmwires/README.md` (table des libs) et le firmware utilisateur (`VERSION.md`) si le contrat change.
