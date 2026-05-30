# `firmwires/shared/` — Bibliothèques communes IoT n3

Bibliothèques PlatformIO utilisées par les firmwares ESP32 du projet IoT n3 (n3pp, msp, uploadphotosserver, et — pour certaines — ffp5cs).

Chaque dossier contient :

- `library.json` : nom, version, dépendances PlatformIO.
- `src/` : sources C++ Arduino (`.h` + `.cpp`).
- Pas de tests natifs PlatformIO pour l'instant (cf. plan d'audit 2026-05 — Phase 4 tests).

## Inventaire

| Lib | Version | Description courte | Dépendances |
|-----|---------|--------------------|-------------|
| [`n3_analog_sensors`](n3_analog_sensors/) | 1.0.0 | Lecture ADC filtrée (médiane + outliers + EMA). Utilisée par n3pp, msp, ffp5cs. | — |
| [`n3_battery`](n3_battery/) | 1.0.1 | Pont diviseur (délègue à `n3_analog_sensors`). | `n3_analog_sensors ^1.0.0` |
| [`n3_wifi`](n3_wifi/) | 1.1.0 | Connexion WiFi multi-réseaux avec scan+RSSI+BSSID, callbacks. | — |
| [`n3_http`](n3_http/) | 1.1.0 | **Déprécié** : GET/POST minimal HTTPClient avec timeout (`N3_HTTP_TIMEOUT_MS`). Préférer `n3_data`. | `n3_common ^1.3.0` |
| [`n3_data`](n3_data/) | 1.0.0 | POST `application/x-www-form-urlencoded` avec HMAC body (`X-Signature`) et HMAC FFP3 (timestamp+signature dans le body). Timeout 5 s. | `n3_hmac`, `n3_common` |
| [`n3_hmac`](n3_hmac/) | 1.0.0 | HMAC-SHA256 via mbedtls + helper d'attache du header `X-Signature`. | — |
| [`n3_mail`](n3_mail/) | 1.0.0 | Envoi email SMTP via ESP Mail Client (helper debug body). | `mobizt/ESP Mail Client` |
| [`n3_time`](n3_time/) | 1.0.0 | Sauvegarde/restauration heure RTC en flash NVS, raison de réveil. | `fbiego/ESP32Time` |
| [`n3_common`](n3_common/) | 1.4.0 | OTA HTTP distant avec vérif sha256 + ECDSA P-256 (`n3_ota`), constantes `n3_defaults.h`, parsing JSON outputs `n3_outputs_json` (factorisation 2026-05). | `bblanchon/ArduinoJson ^7.4.3`, `arduino-libraries/Arduino_JSON ^0.2.0` |
| [`n3_sleep`](n3_sleep/) | 1.0.0 | Configuration et démarrage du deep sleep ESP32 (timer + GPIO ext0). | — |
| [`n3_display`](n3_display/) | 1.0.0 | Init OLED SSD1306 (sondage I2C + retry). | `adafruit/SSD1306` |
| [`libn3_iot`](libn3_iot/) | 1.1.0 | Drivers capteurs génériques (DHT, analogique filtré via `n3_analog_sensors`, DS18B20). Conservé pour compat ; nouveau code → libs ciblées. | `adafruit/DHT sensor library`, `n3_analog_sensors` |

## Intégration

Chaque firmware référence `shared/` via `lib_extra_dirs = ../shared` dans son `platformio.ini` (n3pp, msp, uploadphotosserver, ffp5cs).

Les bornes physiques (DHT -40..80 °C, 0..100 %), les fallbacks (DHT 20 °C / 50 %, DS18B20 20 °C), et les sentinelles de capteurs déconnectés sont **dans les firmwares** (pas dans les libs) car les seuils peuvent varier selon le matériel. Les libs fournissent les briques (filtrage, détection isnan, retours codés) et laissent la décision au firmware.

## Sécurité

- **OTA** : `n3_common/n3_ota` vérifie le sha256 du firmware téléchargé puis la signature ECDSA P-256 si le champ `signature` est présent dans `metadata.json`. Cf. [`serveur/docs/OTA_N3PP_MSP.md`](../../serveur/docs/OTA_N3PP_MSP.md).
- **POST** : `n3_data` peut signer le timestamp avec `API_SIG_SECRET` (alignement contrat FFP3, serveur valide via `HmacAuthTrait`). Cf. [`serveur/docs/API_MSP1_N3PP.md`](../../serveur/docs/API_MSP1_N3PP.md).
- **Secrets** : tous les credentials sont dans `firmwires/credentials.h` (non versionné). Modèle : `firmwires/credentials.h.example`. Clé OTA privée : sous `scripts/ota_keys/` (gitignored).

## Versionnage

Toute modification d'une lib doit incrémenter sa `version` dans `library.json` (semver). Aligner aussi `firmwires/README.md` (table des libs) et le firmware utilisateur (`VERSION.md`) si le contrat change.
