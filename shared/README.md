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
- `n3_hmac` (`test_hmac`) : contrat du wrapper (formatage hex 64 car., garde-fou de taille, header `X-Signature`). La justesse crypto reste validée sur cible (vrai mbedtls).
- Logique pure mutualisée depuis ffp5cs (mêmes assertions que les suites ffp5cs d'origine, parité octet-identique) : `test_epoch_util`, `test_clock_decision`, `test_uptime_format` (lib `n3_time`) ; `test_sleep_decision`, `test_reset_reason`, `test_login_throttle` (lib `n3_common`).

Note : `pio test` est lancé **par suite** (`-f`) car le runner natif multi-suites de PlatformIO échoue à enchaîner plusieurs binaires de test (voir la CI `.github/workflows/firmware-ci.yml`). À étendre aux autres libs à logique pure.

## Inventaire

| Lib | Version | Description courte | Dépendances |
|-----|---------|--------------------|-------------|
| [`n3_analog_sensors`](n3_analog_sensors/) | 1.0.0 | Lecture ADC filtrée (médiane + outliers + EMA). Utilisée par n3pp, msp, ffp5cs. | — |
| [`n3_battery`](n3_battery/) | 1.0.1 | Pont diviseur (délègue à `n3_analog_sensors`). | `n3_analog_sensors ^1.0.0` |
| [`n3_wifi`](n3_wifi/) | 1.1.0 | Connexion WiFi multi-réseaux avec scan+RSSI+BSSID, callbacks. | — |
| [`n3_http`](n3_http/) | 1.1.0 | **Déprécié** : GET/POST minimal HTTPClient avec timeout (`N3_HTTP_TIMEOUT_MS`). Préférer `n3_data`. | `n3_common ^1.3.0` |
| [`n3_data`](n3_data/) | 1.2.0 | POST/GET URL-encoded, HMAC, timeout 5 s. Logs `[SERVER][POST/GET] Verdict` + stats `N3NetStatsSnapshot` pour rapports mail. | `n3_hmac`, `n3_common` |
| [`n3_hmac`](n3_hmac/) | 1.0.0 | HMAC-SHA256 via mbedtls + helper d'attache du header `X-Signature`. | — |
| [`n3_mail`](n3_mail/) | 1.1.0 | Envoi SMTP, debug body et rapport reseau periodique (`n3MailBuildNetReportBody`). | `n3_data`, `mobizt/ESP Mail Client` |
| [`n3_time`](n3_time/) | 1.1.0 | Sauvegarde/restauration heure RTC en flash NVS, raison de réveil. Logique pure mutualisée (depuis ffp5cs) : `n3_epoch_util` (validation epoch anti-overflow 32-bit), `n3_clock_decision` (plausibilité NTP + dérive PPM), `n3_uptime_format`. | `fbiego/ESP32Time` |
| [`n3_common`](n3_common/) | 1.5.0 | OTA HTTP distant avec vérif sha256 + ECDSA P-256 (`n3_ota`), constantes `n3_defaults.h`, parsing JSON outputs `n3_outputs_json` (factorisation 2026-05). Logique pure mutualisée (depuis ffp5cs) : `n3_sleep_decision` (délai de sommeil adaptatif), `n3_reset_reason` (libellé + classification crash), `n3_login_throttle` (anti-brute-force). | `bblanchon/ArduinoJson ^7.4.3`, `arduino-libraries/Arduino_JSON ^0.2.0` |
| [`n3_sleep`](n3_sleep/) | 1.0.0 | Configuration et démarrage du deep sleep ESP32 (timer + GPIO ext0). | — |
| [`n3_display`](n3_display/) | 1.0.1 | Init OLED SSD1306 (sondage I2C + retry). | `adafruit/Adafruit SSD1306` |

## Intégration

Chaque firmware référence `shared/` via `lib_extra_dirs = ../shared` dans son `platformio.ini` (n3pp, msp, uploadphotosserver, ffp5cs).

Les bornes physiques (DHT -40..80 °C, 0..100 %), les fallbacks (DHT 20 °C / 50 %, DS18B20 20 °C), et les sentinelles de capteurs déconnectés sont **dans les firmwares** (pas dans les libs) car les seuils peuvent varier selon le matériel. Les libs fournissent les briques (filtrage, détection isnan, retours codés) et laissent la décision au firmware.

## Sécurité

- **OTA** : `n3_common/n3_ota` vérifie le sha256 du firmware téléchargé puis la signature ECDSA P-256 si le champ `signature` est présent dans `metadata.json`. Cf. [`serveur/docs/OTA_N3PP_MSP.md`](../../serveur/docs/OTA_N3PP_MSP.md).
- **POST** : `n3_data` peut signer le timestamp avec `API_SIG_SECRET` (alignement contrat FFP3, serveur valide via `HmacAuthTrait`). Cf. [`serveur/docs/API_MSP1_N3PP.md`](../../serveur/docs/API_MSP1_N3PP.md).
- **Secrets** : tous les credentials sont dans `firmwires/credentials.h` (non versionné). Modèle : `firmwires/credentials.h.example`. Clé OTA privée : sous `scripts/ota_keys/` (gitignored).

## Versionnage

Toute modification d'une lib doit incrémenter sa `version` dans `library.json` (semver). Aligner aussi `firmwires/README.md` (table des libs) et le firmware utilisateur (`VERSION.md`) si le contrat change.
