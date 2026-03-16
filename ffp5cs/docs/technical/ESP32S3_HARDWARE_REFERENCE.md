# Référence matériel ESP32-S3 (FFP5CS)

Ce document décrit le **modèle exact** de l’ESP32-S3 utilisé comme référence pour les builds S3 du projet FFP5CS, et le lien avec les environnements PlatformIO.

## Modèle exact (référence)

Caractéristiques identifiées par les logs au boot et par esptool :

| Caractéristique | Valeur |
|-----------------|--------|
| **Chip** | ESP32-S3 (QFN56) |
| **Révision silicium** | v0.2 |
| **Flash** | 16 Mo |
| **PSRAM** | 8 Mo (Embedded, AP_3v3) |
| **CPU** | 2 cœurs, 240 MHz |
| **ROM** | esp32s3-20210327 (Build Mar 27 2021) |

Référence module : type **N16R8** (16 Mo flash + 8 Mo PSRAM).

## Environnements PlatformIO S3

- **Sans PSRAM** (boot stable, défaut) : `wroom-s3-base`, `wroom-s3-test`, `wroom-s3-prod`  
  - 16 Mo flash, PSRAM désactivée dans le sdkconfig.  
  - Utiliser pour la stabilité au boot et les builds de test/prod courants.

- **Avec PSRAM** (matériel N16R8) : `wroom-s3-test-psram`  
  - 16 Mo flash + 8 Mo PSRAM, flag `BOARD_HAS_PSRAM` défini, **`-mfix-esp32-psram-cache-issue`** en build pour limiter les échecs flash/boot (aligné LVGL_Widgets).  
  - Utiliser lorsque l’application doit exploiter la PSRAM (heap étendu, buffers, etc.).
  - **Validation recommandée** : script **`run_s3_psram_validation.ps1`** (build + erase + flash + monitor 2 ou 5 min + analyse). Voir aussi `docs/technical/ESP32S3_TG1WDT_BOOT_FIX.md`.
- **Build sous Windows** : le pre-script **`tools/pio_ensure_lib_dirs_windows.py`** pré-crée les répertoires de build des libs dont le nom contient des espaces (ex. « Adafruit GFX Library »), ce qui évite l’erreur « opening dependency file … No such file or directory ». Il s’exécute automatiquement pour `wroom-s3-test-psram` sous Windows uniquement.

Pour les cartes **8 Mo flash** + PSRAM (DevKit 8 Mo), utiliser l’env `wroom-s3-test-devkit` ; pour le matériel 16 Mo flash + 8 Mo PSRAM (N16R8), utiliser `wroom-s3-test-psram`.

### Choix d’environnement (référence rapide)

| Besoin | Environnement(s) à utiliser |
|--------|-----------------------------|
| **WiFi sur S3** | `wroom-s3-test` ou `wroom-s3-prod` (sans PSRAM), ou `wroom-s3-test-psram` (WiFi réactivée après NVS). |
| **PSRAM requise, WiFi tentée** (N16R8) | `wroom-s3-test-psram` (Serial activé après NVS, init WiFi et AP de secours réactivées ; en cas de blocage esp_wifi_init, voir option de repli ci-dessous). |

**Boot avec PSRAM (TG1WDT)** : l’init PSRAM dépasse 300 ms et l’Interrupt Watchdog (IWDT) du bootloader peut provoquer une boucle de reset (TG1WDT_SYS_RST). Le correctif combine : (1) un pre script (`tools/pio_s3_psram_patch_iwdt.py`) qui désactive l'IWDT dans le variant qio_opi (`CONFIG_ESP_INT_WDT=0`) ; (2) en début de `setup()`, désactivation runtime de l'IWDT (Timer Group 1 / MWDT1) via le HAL WDT (`wdt_hal_disable`), pour couvrir le cas où le bootloader l'aurait déjà activé. En cohérence avec la doc du projet test psram s3 2. Pour un **boot stable** sur N16R8, utiliser **wroom-s3-test** (sans PSRAM) si la PSRAM n'est pas requise ; **wroom-s3-test-psram** avec ce correctif pour exploiter la PSRAM.

---

## Résumé de la situation (S3 PSRAM / WiFi)

| Élément | Situation |
|--------|------------|
| **Matériel cible** | ESP32-S3 N16R8 (16 Mo flash, 8 Mo PSRAM), variant qio_opi. |
| **Stack actuelle** | platformio/espressif32 **6.13.0**, framework arduino-esp32 **~3.0.x** (bundlé). Dernière release arduino-esp32 : **3.3.7** (ESP-IDF 5.5.2), non utilisable telle quelle avec cette plateforme (structure du bundle différente). |
| **Serial après NVS** | Sur **wroom-s3-test-psram**, `Serial.begin()` est appelé **après** l'init NVS/storage et **avant** `connectWifi()`, afin que le chemin WiFi (wifi_manager) puisse utiliser `Serial` sans blocage au boot. |
| **Init WiFi** | L'init WiFi (`WiFi.mode()`, connect, AP de secours) est à nouveau **tentée** sur wroom-s3-test-psram. Si un blocage en `esp_wifi_init()` est observé, documenter et utiliser l'option de repli (macro `FFP5CS_S3_PSRAM_WIFI_DISABLED` dans wifi_manager.cpp). |
| **Ce qui fonctionne** | Boot stable (correctif IWDT, BOOT_LOG NVS), LittleFS, NVS, tâches, OLED, loop. Validation : `run_s3_psram_validation.ps1`. |
| **Choix d’env** | **WiFi sur S3** → `wroom-s3-test` / `wroom-s3-prod` ou `wroom-s3-test-psram` (WiFi réactivée). **Option de repli** : si blocage persistant, définir `FFP5CS_S3_PSRAM_WIFI_DISABLED` pour désactiver uniquement le chemin WiFi S3 PSRAM. |
| **Évolution** | Réactiver une tentative d’init WiFi (ex. dans `tryDelayedModeInit()`) lorsque **platformio/espressif32** fournira un bundle avec **arduino-esp32 3.3.x**. Suivre les releases [platform-espressif32](https://github.com/platformio/platform-espressif32) et [arduino-esp32](https://github.com/espressif/arduino-esp32/releases). |

### Évolution – PSRAM + WiFi (axe 2)

Pour débloquer **PSRAM + WiFi** sur S3, revalider périodiquement :

1. **Releases plateformes** : [platformio/platform-espressif32](https://github.com/platformio/platform-espressif32) (début 2025 : v6.13.0, Arduino 2.0.17 / IDF 4.4.7) et [pioarduino/platform-espressif32](https://github.com/pioarduino/platform-espressif32) (arduino-esp32 3.3.7 / IDF 5.5.2 pour WROOM). Vérifier si une version récente propose un build S3 + qio_opi qui link correctement.
2. **Issue arduino-esp32 #8377** (ESP32-S3 hang UART/Serial au boot avec PSRAM) : fermée en **Wontfix** (comportement IDF UART driver / timing). Contournement côté FFP5CS : `ets_printf()` pour les logs boot sur S3 PSRAM. Si un correctif ou une évolution IDF/Arduino corrige le blocage `esp_wifi_init()` avec PSRAM, réactiver une tentative d’init dans `tryDelayedModeInit()` et valider avec `run_s3_psram_validation.ps1`.
3. **Actions si correctif disponible** : créer un env de test S3 sur la nouvelle plateforme (copie de wroom-s3-base / wroom-s3-test-psram), build + flash + monitor 5 min + analyse ; si WiFi+PSRAM fonctionnent, basculer les env S3 sur cette stack.

---

## Comportement firmware S3 PSRAM (wroom-s3-test-psram)

Sur l’env **wroom-s3-test-psram**, le firmware applique des adaptations pour éviter blocages et starvation au boot. Les points ci-dessous décrivent les changements pertinents et les **recommandations** à respecter lors de toute modification du code concerné.

### Serial (UART)

- **Configuration** : l’env **wroom-s3-test-psram** utilise **Serial sur UART** (`ARDUINO_USB_CDC_ON_BOOT=0`), comme wroom-s3-test. Le même port (ex. COM7) sert au flash et au moniteur série.
- **Adaptation** : dans les chemins exécutés au boot et dans les tâches (sensor, web, auto, net), le code sous `#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)` utilise **`ets_printf()`** (sortie UART0) au lieu de `Serial` pour les messages de boot et de diagnostic, afin d’éviter tout blocage si le buffer UART n’est pas encore lu.
- **Recommandation** : ne pas réintroduire de `Serial.*` avant l'appel à `Serial.begin()` (NVS, FS). Après cet appel (boot, `AppTasks::start`, début des tâches FreeRTOS, fin de `setup()`) pour l’env S3 PSRAM. En dehors du boot, `Serial` est utilisable sur le port UART.

#### Dans quelle mesure les autres Serial peuvent être bloquants ?

`Serial` (UART) écrit dans un **buffer TX** (souvent 256–512 octets). Dès que ce buffer est **plein**, tout `Serial.print` / `println` / `printf` **bloque** jusqu’à ce qu’il y ait de la place (donc tant que personne ne lit le port ou que le débit 115200 ne suffit pas à vider le buffer).

- **Risque élevé (boot / setup)**  
  - **app.cpp** : tout `Serial` après `Serial.begin()` jusqu’à la fin de `setup()` (raison de reset, chip, watchdog, événements, « Init done »). Le marqueur « BOOT FFP5CS » est déjà envoyé en `ets_printf` **avant** `Serial.begin()` pour que la validation reste verte même si `Serial.begin()` ou les premiers `Serial` bloquent.  
  - **system_boot.cpp** : `initializeStorage()` et le reste du boot (LittleFS, NVS, OTA, time, I2C) utilisent beaucoup de `Serial` sans variante `ets_printf` pour S3 PSRAM. Si le buffer se remplit pendant ces phases, le prochain `Serial` peut bloquer.  
  - **Callbacks diag (app.cpp)** : `ffp5cs_diag_*` et `initVariant` n’utilisent plus que `ets_printf` pour S3 PSRAM (plus de `Serial`), pour éviter tout blocage pendant `initArduino()`.

- **Risque moyen (démarrage des tâches)**  
  - **app_tasks.cpp** : premiers messages de netTask, webTask, automationTask au démarrage. Si le buffer est déjà plein à ce moment-là, ces `Serial` peuvent bloquer la tâche.

- **Risque plus faible (loop, logs périodiques)**  
  - **wifi_manager**, **loop**, logs périodiques dans les tâches : le buffer a en général le temps de se vider entre deux logs. Le risque augmente si les logs sont très fréquents ou si le moniteur n’est pas connecté / pas lu.

En résumé : **tout** `Serial.*` peut bloquer si le buffer TX est plein. Pour S3 PSRAM, les chemins critiques (avant/début de `setup()`, callbacks diag, marqueur de boot) utilisent `ets_printf` ; le reste du boot et des tâches reste en `Serial` et peut donc bloquer si le moniteur ne lit pas assez vite ou n’est pas connecté.

### Priorités des tâches FreeRTOS

- **Configuration** : pour S3 PSRAM, **automationTask** et **netTask** utilisent les **mêmes priorités** que les autres envs (3 et 2). La loop Arduino cède le CPU au début de chaque cycle avec **`vTaskDelay(pdMS_TO_TICKS(1))`**, ce qui évite la starvation de la loop tout en gardant le comportement identique aux autres envs.
- **Recommandation** : ne pas supprimer le `vTaskDelay(1)` en loop pour S3 PSRAM sans valider la stabilité (run 5 min avec `run_s3_psram_validation.ps1`).

### Fin de setup() et affichage OLED

- **Contexte** : en fin de `setup()`, les appels `Serial.println` / `LOG_INFO` / `LOG_ERROR` bloquent sur S3 si le CDC n’est pas lu. De plus, dans `finalizeDisplay()` (avant « Init ok »), `DisplayView::forceEndSplash()` et `DisplayView::flush()` contenaient des `Serial.*` qui bloquaient et empêchaient l’affichage « Diag: Init ok » sur l’OLED.
- **Adaptation** :
  - Pour S3 PSRAM, la fin de `setup()` n’utilise pas `Serial` ni les macros `LOG_*` ; seuls des `ets_printf` indiquent « setup done » et « init done ».
  - Dans `display_view.cpp`, pour S3 PSRAM : `forceEndSplash()` utilise `ets_printf` au lieu de `Serial.println` ; le log debug dans `flush()` (si `FEATURE_DIAG_OLED_LOGS`) est désactivé pour éviter tout blocage dans le chemin d’affichage.
- **Recommandation** : ne pas ajouter de `Serial.*` ou `LOG_*` dans `finalizeDisplay()`, `forceEndSplash()`, ni dans le chemin d’affichage diag (ex. `showDiagnostic`) sans garde S3 PSRAM / `ets_printf`.

### WiFi (S3 PSRAM)

- **Contexte** : sur S3 avec PSRAM, le chemin WiFi utilisait `Serial` avant tout `Serial.begin()` (NVS utilise BOOT_LOG), ce qui bloquait au premier log WiFi. Un blocage en `esp_wifi_init()` avait aussi été observé (arduino-esp32 #8377).
- **État actuel** : Serial est activé après NVS (app.cpp, juste avant `connectWifi()`). L'init WiFi (`WiFi.mode()`, connect, AP de secours) est **réactivée** : `connect()` et `startFallbackAP()` appellent `WiFi.mode()` et lancent le flux normal. Si un blocage en `esp_wifi_init()` est encore observé, définir **`FFP5CS_S3_PSRAM_WIFI_DISABLED`** et réintroduire les early returns dans wifi_manager.cpp.

### Loop et affichage (S3 PSRAM)

- **Configuration** : la loop appelle **`wifi.loop(&oled)`** pour S3 PSRAM comme pour les autres envs (messages diag sur l’OLED pendant le scan/connexion). L’init WiFi différée évite le blocage au boot ; le `vTaskDelay(1)` en loop assure un yield suffisant.

### Yield au boot (phases longues)

- **Contexte** : les phases longues (LittleFS, NVS, init display, init peripherals, finalizeDisplay) peuvent tenir le CPU sans relâcher le scheduler et contribuer au déclenchement du TWDT.
- **Adaptation** : pour S3 PSRAM, des **`vTaskDelay(pdMS_TO_TICKS(1))`** sont insérés après LittleFS et après NVS dans `system_boot.cpp`, et après initDisplay, initPeripherals et finalizeDisplay dans `app.cpp`, afin de laisser le watchdog être nourri.
- **Recommandation** : ne pas supprimer ces délais sans vérifier la stabilité (run 5 min avec `run_s3_psram_validation.ps1`).

### Résumé des recommandations

| Domaine | Recommandation |
|--------|----------------|
| Logs boot / tâches | Utiliser `ets_printf` (ou garde S3 PSRAM) dans les chemins exécutés au boot et au démarrage des tâches si le moniteur est sur UART. |
| Priorités tâches | S3 PSRAM : priorités 3/2 comme les autres envs ; garder `vTaskDelay(1)` en loop. |
| Setup / OLED | Pas de `Serial` ni `LOG_*` en fin de setup ni dans `forceEndSplash` / `flush` diag pour S3 PSRAM. |
| WiFi S3 PSRAM | Serial après NVS ; init WiFi et AP de secours réactivées. En cas de blocage esp_wifi_init, définir `FFP5CS_S3_PSRAM_WIFI_DISABLED`. |
