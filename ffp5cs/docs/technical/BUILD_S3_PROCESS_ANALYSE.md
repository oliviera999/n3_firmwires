# Analyse du process de build wroom-s3-test

**Matériel de référence S3** : ESP32-S3 (QFN56) révision v0.2, 16 Mo flash, 8 Mo PSRAM (N16R8). Env sans PSRAM : `wroom-s3-base` / `wroom-s3-test` / `wroom-s3-prod`. Env avec PSRAM pour ce matériel : `wroom-s3-test-psram`. Détails : [ESP32S3_HARDWARE_REFERENCE.md](ESP32S3_HARDWARE_REFERENCE.md).

## Enchaînement (sans -SkipBuild)

1. **Script erase_flash_fs_monitor_5min_analyze.ps1 (étape 0)**
   - `patch_espressif32_custom_sdkconfig_only.py` → patch plateforme pour custom_sdkconfig
   - `patch_arduino_early_init_variant.py` → earlyInitVariant() dans initArduino()
   - `patch_arduino_diag_after_nvs.py` → ffp5cs_diag_* pour debug boot
   - `patch_arduino_libs_s3_wdt.py` → sdkconfig esp32s3 (WDT, coredump)
   - Suppression sdkconfig dans `framework-arduinoespressif32-libs` → force recompil des libs IDF
   - **clean_s3_build.ps1** → supprime `.pio/libdeps/wroom-s3-test`
   - **pio run -e wroom-s3-test -t fullclean** → supprime tout le build
   - **pio run -e wroom-s3-test** → build complet (voir ci-dessous)

2. **pio run -e wroom-s3-test (détail)**
   - **Pre-scripts** (platformio.ini) : `pio_pre_apply_arduino_patches.py`, `pio_add_mklittlefs_path.py`, puis S3 : `s3_idf_git_data_fix.py`, `s3_patch_esp_insights_mqtt.py`, `s3_patch_wpa_supplicant_wno_error.py`, `s3_patch_ldgen_fragments_file.py`, `s3_patch_platform_espidf_ldgen.py`
   - **Plateforme pioarduino** : déclenche "Compile Arduino IDF libs" (call_compile_libs) car custom_sdkconfig est défini
   - **Phase 1** : CMake + Ninja compilent les composants ESP-IDF (esp_psram, bootloader_support, esp_wifi, etc.) avec le sdkconfig mergé (sdkconfig_s3_wdt.txt) → durée ~10–20 min
   - **Phase 2** : copie des .a vers `framework-arduinoespressif32-libs/esp32s3/qio_opi/`
   - **Phase 3** : compilation des sources Arduino + projet (libs, src/) → durée ~5–15 min
   - **Link** : génération firmware.elf puis firmware.bin

## Points de vigilance / bugs potentiels

- **fullclean systématique** : sans -SkipBuild, à chaque run on fait fullclean donc recompil intégrale (20–40 min). Pas un bug mais la cause de la lenteur.
- **CONFIG_ESP_TASK_WDT_TIMEOUT_S redefined** : sdkconfig.h généré contient 5, alors que build_flags passe -DCONFIG_ESP_TASK_WDT_TIMEOUT_S=300. Le bootloader est compilé avec le sdkconfig (5 s) ; l’app avec le flag (300 s). Redéfinition = warning seulement, pas d’échec de build.
- **ARDUINO_USB_CDC_ON_BOOT redefined** : la board 4d définit ARDUINO_USB_CDC_ON_BOOT=1, on override à 0 dans build_flags → warning bénin.
- **Options C++ passées à des .c** : -fno-rtti, -fno-threadsafe-statics, -fno-use-cxa-atexit sont pour C++ ; certains fichiers IDF sont en C → warnings, pas d’échec.
- **Error 3221225786** : code Windows = processus tué (ex. Ctrl+C). Pas un bug du build.
- **firmware.elf does not exist** : conséquence d’un build interrompu avant l’étape link.

## Build propre sans patches (wroom-s3-test)

L’env **wroom-s3-test** n’applique plus les patches du core (earlyInitVariant, diag_after_nvs, libs_s3_wdt). Pour être certain que ce sont les **sources vierges** des packages qui sont compilées (et pas des fichiers déjà patchés par un build précédent ou un autre env S3), il faut supprimer les packages framework puis faire un fullclean :

1. Fermer Cursor, moniteur série et tout processus utilisant le projet (évite WinError 32).
2. **Supprimer les packages + fullclean :**
   ```powershell
   .\tools\clean_s3_build.ps1 -IncludeFramework -IncludeFrameworkLibs -FullClean
   ```
3. **Build :**
   ```powershell
   pio run -e wroom-s3-test
   ```
   PlatformIO réinstallera `framework-arduinoespressif32` et `framework-arduinoespressif32-libs` au premier build ; les sources seront vierges, aucun patch ne sera appliqué pour wroom-s3-test.

Sans `-IncludeFramework` / `-IncludeFrameworkLibs`, un simple `pio run -e wroom-s3-test -t fullclean` puis `pio run -e wroom-s3-test` nettoie uniquement le cache du projet ; si les packages ont déjà été patchés (ex. par un build wroom-s3-prod), ce sont encore ces fichiers modifiés qui seront compilés.

## Recommandation

- Utiliser **-SkipBuild** pour les runs erase/flash/monitor/analyse une fois un build réussi disponible.
- Pour obtenir un build complet : lancer **une fois** `pio run -e wroom-s3-test` (sans fullclean si on veut reprendre un build partiel) et attendre la fin sans interrompre.

---

## Capture du log de compile (build_wroom_s3_test.log)

**Commande utilisée :** `pio run -e wroom-s3-test 2>&1 | Tee-Object -FilePath build_wroom_s3_test.log`

- Pendant la **compile** il n’y a **pas de moniteur série** : l’ESP32 ne tourne pas, tout se passe sur le PC. La "capture" est donc le **log du build** (stdout/stderr), pas la série.
- Le fichier **build_wroom_s3_test.log** en racine contient toute la sortie (pre-scripts, config, compilation IDF puis Arduino, link si atteint).

### Ce qu’on voit dans le log (analyse type)

- **Début** : patches déjà appliqués, pre-scripts FFP5CS OK (mklittlefs, LittleFS, esp_littlefs, -Wno-error), "Compile Arduino IDF libs", sdkconfig Replace/Add (SPIRAM, CONFIG_ESP_TASK_WDT_TIMEOUT_S=300, etc.).
- **Phase IDF** : compilation de très nombreux composants (lwip, bootloader_support, esp_psram, esp_wifi, esp_gdbstub, esp_hid, esp_http_server, …). Beaucoup de **warnings** :
  - `CONFIG_ESP_TASK_WDT_TIMEOUT_S` redefined (sdkconfig.h = 5, command-line = 300) → bénin.
  - `ARDUINO_USB_CDC_ON_BOOT` redefined → bénin.
  - Options C++ passées à des fichiers C (-fno-rtti, etc.) → bénin.
- **Phase Arduino** : "Linking .pio\build\wroom-s3-test\firmware.elf" peut apparaître plusieurs fois (SCons relance le link quand des .o changent).
- **Fin attendue** : "Building .pio\build\wroom-s3-test\firmware.bin", "SUCCESS", et présence de `.pio/build/wroom-s3-test/firmware.elf` et `firmware.bin`.

Si le log s’arrête avant "SUCCESS" : build encore en cours, interrompu (Ctrl+C), ou échec. Vérifier la toute fin du fichier et l’absence de ligne `error:` ou `Error 1`.
