# Version uploadphotosserver (ESP32-CAM unifié)

Version actuelle : **2.36** (définie dans `include/config.h`).

---

## Historique

| Version | Date | Modifications |
|---------|------|---------------|
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
