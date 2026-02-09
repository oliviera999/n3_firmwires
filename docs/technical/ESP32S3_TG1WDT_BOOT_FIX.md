# Correction boot loop TG1WDT sur ESP32-S3

## Problème
Sur ESP32-S3 (wroom-s3-test), boucle de reset **TG1WDT_SYS_RST** au boot : l’Interrupt WDT (300 ms par défaut) coupe avant d’atteindre `setup()`. Aucune ligne « BOOT FFP5CS » dans les logs.

## Cause
La lib Arduino précompilée (`framework-arduinoespressif32-libs`) utilise `CONFIG_ESP_INT_WDT_TIMEOUT_MS=300`. Le boot (ROM + framework + PSRAM) dépasse 300 ms.

## Solution
1. **`CONFIG_ESP_INT_WDT=n`** : désactiver l’IWDT (dans `platformio.ini` + `sdkconfig.defaults`).
2. **Patch plateforme** : `python tools/patch_espressif32_custom_sdkconfig_only.py` pour que `custom_sdkconfig` ne force pas `espidf`, afin que HandleArduinoIDFsettings s’exécute.
3. La plateforme doit **recompiler la lib Arduino** (via HandleArduinoIDFsettings) pour que le correctif soit pris en compte.

## Fichiers modifiés
- `platformio.ini` : `custom_sdkconfig = file://sdkconfig_s3_wdt.txt` (obligatoire : le `#` en ligne INI serait interprété comme commentaire).
- `sdkconfig_s3_wdt.txt` (racine) : `# CONFIG_ESP_INT_WDT is not set`, `CONFIG_ESP_TASK_WDT_TIMEOUT_S=300`, `CONFIG_ARDUINO_LOOP_STACK_SIZE=32768`.
- `sdkconfig.defaults` : idem en cohérence/doc.
- `tools/patch_espressif32_custom_sdkconfig_only.py` : patch de la plateforme (custom_sdkconfig seul n’ajoute pas espidf).
- `tools/patch_arduino_libs_s3_wdt.py` : patch du sdkconfig source du package (optionnel).

## Étapes de validation

**Cas normal : projet dans un chemin sans espace** (recommandé). Tout peut être lancé depuis la racine du projet :

- **Script tout-en-un (PowerShell)** :
  ```powershell
  .\run_s3_validation.ps1 -Port COM7
  ```
- **Ou script batch** :
  ```batch
  run_s3_fix_full.bat
  ```
- **Ou manuellement** (depuis la racine du projet) :
  1. `python tools/patch_espressif32_custom_sdkconfig_only.py`
  2. `pio run -e wroom-s3-test` (peut prendre 15–20 min si recompile lib Arduino)
  3. Vérifier dans la sortie : `*** Compile Arduino IDF libs ***` et `Replace:` ou `Add:` pour CONFIG_ESP_INT_WDT
  4. `.\erase_flash_fs_monitor_5min_analyze.ps1 -Environment wroom-s3-test -Port COM7 -DurationMinutes 1`
  5. Dans le dernier `monitor_1min_*.log` : au moins une ligne **BOOT FFP5CS**, absence de **rst:0x8 (TG1WDT_SYS_RST)**

Les scripts `run_s3_build_from_safe_path.bat` et `run_s3_fix_via_subst.bat` détectent automatiquement l’absence d’espace dans le chemin et exécutent alors le build (et optionnellement le workflow) directement depuis le projet, sans copie ni lecteur virtuel.

---

**Cas particulier : projet dans un chemin avec espaces** (ex. « Mon Drive »). Le build SCons/PlatformIO peut rester bloqué après `------`. Dans ce cas, utiliser :

- **run_s3_build_from_safe_path.bat** : copie le projet vers un dossier sans espace (défaut `C:\ffp5cs_build`), build dedans, recopie `firmware.bin` + `partitions.bin` dans le projet. Option `workflow` pour enchaîner erase/flash/monitor depuis le dossier de build :
  ```batch
  run_s3_build_from_safe_path.bat workflow
  ```
  (Variable d’environnement `BUILD_ROOT` pour changer le dossier.)
- **run_s3_fix_via_subst.bat** : lecteur virtuel `P:` pointant sur le projet (subst), build depuis `P:\`. Moins fiable que la copie sur certains setups.

## PSRAM et SRAM active (S3)
- Board 4d R8N16 = 8 MB PSRAM OPI (qio_opi). Activé par la définition de la carte. `BOARD_HAS_PSRAM` dans build_flags.
- `sdkconfig_s3_wdt.txt` : `CONFIG_SPIRAM=y` explicite pour garantir la PSRAM (SRAM externe) active ; table de partitions personnalisée `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME` pour éviter « partitions.csv not found ».
- `include/config.h` : NET_TASK_STACK_SIZE = 12288 pour BOARD_S3 (évite stack canary netTask).
- CONFIG_ARDUINO_LOOP_STACK_SIZE=32768 (évite stack canary loopTask). Voir DIAGNOSTIC_BACKTRACE_S3_2026-02-04.md.

## Build avec IDF en tarball (head-ref)
- Quand `framework-espidf` est installé via tarball (env wroom-s3-test), CMake échoue sur `GetGitRevisionDescription` (fichier `head-ref` absent). Le pre-script `tools/s3_idf_git_data_fix.py` crée `CMakeFiles/git-data/head-ref` pour débloquer la config. Activé automatiquement pour les envs `wroom-s3-*`.

## Échec build : https_server.crt.S (esp_insights)

### Symptôme
En fin de build S3, erreur du type :
```text
*** [.pio\build\wroom-s3-test\.pio\build\wroom-s3-test\https_server.crt.S.o] Source `.pio\build\wroom-s3-test\https_server.crt.S' not found
```

### Contexte
- **ESP Insights** (`managed_components/espressif__esp_insights`) est un agent de télédiagnostic Espressif (envoi de métriques/crash vers le cloud). Il est tiré comme dépendance IDF lors du build S3 (présent dans `dependencies.lock` pour la cible `esp32s3`), même si le **code applicatif FFP5CS** ne l’utilise pas (aucune référence dans `src/` ni `include/`).
- Le composant embarque un certificat TLS selon le transport choisi :
  - **HTTPS** (défaut) : `server_certs/https_server.crt` → converti en fichier assembleur `.S` par `target_add_binary_data(… TEXT)` pour être lié au binaire.
  - **MQTT** : `server_certs/mqtt_server.crt` (si `CONFIG_ESP_INSIGHTS_TRANSPORT_MQTT=y`).
- Les certificats existent bien dans le composant (référencés dans `CHECKSUMS.json` : `server_certs/https_server.crt`, `server_certs/mqtt_server.crt`).

### Cause probable
- **Chemin de build dupliqué** : la règle de compilation attend le `.S` dans  
  `.pio/build/wroom-s3-test/.pio/build/wroom-s3-test/`  
  au lieu d’un chemin unique sous `.pio/build/wroom-s3-test/`. C’est typique d’un bug ou d’une particularité de la chaîne **Arduino + ESP-IDF** (plateforme pioarduino) quand elle génère les cibles pour les binaires embarqués : le `BUILD_DIR` est réinjecté une seconde fois dans le chemin de l’artefact `.S`.
- Le fichier `.S` est bien généré par CMake/IDF à partir de `https_server.crt`, mais à un emplacement différent de celui que le recipe de compilation utilise, d’où « Source … not found ».

### Options pour débloquer le firmware S3

1. **Désactiver ESP Insights pour l’env S3** (recommandé si tu n’utilises pas le télédiagnostic)  
   - Le composant est déjà optionnel au runtime (`CONFIG_ESP_INSIGHTS_ENABLED=n` par défaut dans son Kconfig), mais il est tout de même **compilé** tant qu’il est dans le graphe de dépendances.  
   - Pour ne plus le compiler pour S3, il faut qu’il ne soit plus requis pour la cible `esp32s3` :  
     - soit en ne déclarant pas `espressif/esp_insights` (ou la dépendance qui le tire, ex. `espressif/esp_rainmaker`) pour cette cible dans le manifeste IDF du projet,  
     - soit en excluant le répertoire du composant du build pour l’env S3 (mécanisme dépendant de la plateforme / extra_scripts).  
   - Consulter `dependencies.lock` (section `manifest_hash`, `target: esp32s3`) et le fichier manifeste IDF qui fixe les dépendances pour la cible S3.

2. **Forcer le transport MQTT pour Insights**  
   - Dans `sdkconfig_s3_wdt.txt` (ou équivalent) :  
     `CONFIG_ESP_INSIGHTS_TRANSPORT_MQTT=y`  
   - Le composant utilisera alors `mqtt_server.crt` au lieu de `https_server.crt`. Si le bug de chemin ne concerne que le cas HTTPS, le build peut passer (sans changer le fait qu’Insights reste compilé).

3. **Corriger le chemin du .S côté plateforme**  
   - Identifier dans la plateforme pioarduino / espidf le script ou le template qui génère la règle de compilation pour les binaires ajoutés via `target_add_binary_data`, et corriger la construction du chemin pour qu’il n’y ait qu’un seul `BUILD_DIR`. Souvent dans les cmake ou les scripts Python qui appellent le toolchain.

4. **Contournement local (avancé)**  
   - Après configuration CMake, repérer où le `.S` est réellement généré (ex. sous `.pio/build/wroom-s3-test/esp-idf/...` ou un sous-dossier du composant), puis ajouter un extra_script (post ou pre) qui copie ou symlink ce fichier à l’emplacement attendu par la règle qui échoue. Fragile aux mises à jour de la plateforme.

### Résumé
- **Problème** : chemin de build dupliqué pour le `.S` généré à partir de `https_server.crt` (composant esp_insights).  
- **Impact** : échec en liaison pour l’env wroom-s3-test.  
- **Piste privilégiée** : ne pas inclure esp_insights (ou la dépendance qui l’amène) pour la cible esp32s3 si le télédiagnostic cloud n’est pas utilisé, sinon forcer `CONFIG_ESP_INSIGHTS_TRANSPORT_MQTT=y` pour tester si le build passe avec l’autre certificat.

### État actuel (build S3 débloqué)

- **esp_insights / https_server.crt.S**  
  - Script pre `tools/s3_patch_esp_insights_mqtt.py` : patch du `CMakeLists.txt` du composant pour forcer le transport MQTT (certificat `mqtt_server.crt` au lieu de `https_server.crt`).  
  - Pour que le patch survive à la synchro des composants, le script positionne **`IDF_COMPONENT_MANAGER=0`** pour les envs `wroom-s3-*` et invalide le cache CMake après patch.  
  - `sdkconfig_s3_wdt.txt` : `CONFIG_ESP_INSIGHTS_TRANSPORT_MQTT=y` déjà présent.

- **ESP Mail Client / LittleFS**  
  - Erreur connue : la lib utilise SPIFFS par défaut ; sur certains contextes (S3 / pioarduino) « LittleFS was not declared ».  
  - **`include/Custom_ESP_Mail_FS.h`** : définit `ESP_MAIL_DEFAULT_FLASH_FS` sur `LittleFS` (doc mobizt).  
  - **`platformio.ini` (wroom-s3-test)** : `-include <FS.h>` et `-include <LittleFS.h>` dans `build_flags` pour que `LittleFS` soit déclaré avant la compilation des libs.

- **Workflow** : `pio run -e wroom-s3-test` puis `pio run -e wroom-s3-test -t upload` (ou script erase/flash/monitor). En cas d’échec d’install de package (ex. `package-postinstall.py`), relancer ou installer les dépendances à part.

- **wpa_supplicant et mbedtls / -Werror**  
  Script pre `tools/s3_patch_wpa_supplicant_wno_error.py` : patch des CMakeLists IDF (wpa_supplicant + mbedtls) pour remplacer `-Werror` par `-Wno-error`, afin d’éviter l’échec quand des options C++ fuient vers les sources C (pioarduino).

- **« La ligne de commande est trop longue » (sections.ld)**  
  Sous Windows, la génération du script linker (ldgen) peut échouer si la liste des fragments est passée en ligne de commande. Script pre `tools/s3_patch_ldgen_fragments_file.py` : patch de ldgen.py (option `--fragments-list-file`) et ldgen.cmake (écriture de la liste dans un fichier) pour **tous** les packages `framework-espidf*`. Le script `tools/s3_patch_esp_insights_mqtt.py` n’invalide le cache CMake que lorsqu’il modifie réellement le composant (évite de régénérer un build.ninja à chaque run). Le build.ninja généré contient bien une commande ldgen courte (`--fragments-list-file`). Si l’erreur persiste avec `pio run`, la limite peut venir de l’invocation de Ninja par PlatformIO (liste de cibles trop longue). Contournements : (1) chemin de projet court (ex. `C:\f`) ; (2) lancer d’abord la cible ldgen puis le build : `ninja -C .pio/build/wroom-s3-test esp-idf/esp_system/ld/sections.ld` puis `pio run -e wroom-s3-test`.

## LockFileTimeoutError au build
Si `LockFileTimeoutError` apparaît : fermer Cursor (ou IDE qui utilise PlatformIO), lancer depuis un **terminal externe** : `.\run_s3_validation.ps1 -Port COM7`.

## TG0WDT boot loop (Task WDT) et vérification des libs linkées

Sur S3, un reset **TG0WDT_SYS_RST** (~15–16 s après POWERON) peut survenir avant toute sortie série : le Task WDT est démarré au boot (timeout 5 s par défaut dans la lib), et `initVariant()` n’est appelé qu’à la **fin** de `initArduino()` (après `nvs_flash_init()` et `init()`), donc trop tard.

### Où vont les libs recompilées (call_compile_libs)

Dans la plateforme pioarduino (`builder/frameworks/espidf.py`, fonction `idf_lib_copy`) :

1. Les .a compilés avec notre `sdkconfig` sont dans `$PROJECT_BUILD_DIR/$PIOENV/esp-idf/` (ex. `.pio/build/wroom-s3-test/esp-idf/`).
2. Ils sont copiés vers `ARDUINO_FRMWRK_LIB_DIR / chip_variant / "lib"` (ex. `framework-arduinoespressif32-libs/esp32s3/lib/`).
3. Pour S3, **certaines** libs sont ensuite **remplacées** dans le dossier **memory_type** (ex. `qio_opi`) : `libesp_system.a`, `libfreertos.a`, `libbootloader_support.a`, etc. (`_replace_copy` de `lib_dst` vers `mem_var`).
4. `mem_var` = `arduino_libs / chip_variant / board["build.arduino.memory_type"]` (ex. `.../esp32s3/qio_opi/`). C’est ce dossier qui est utilisé par l’étape « Arduino compile » (second `pio run`).
5. Le linker du build Arduino doit donc utiliser les .a présents dans `mem_var`. Pour confirmer : après un build avec « Compile Arduino IDF libs », vérifier la date de modification de `.../esp32s3/qio_opi/libesp_system.a` (doit être récente). Optionnel : `xtensa-esp32s3-elf-nm .../libesp_system.a | findstr task_wdt` pour voir si le TWDT est présent dans cette lib.

Si TG0WDT persiste malgré `CONFIG_ESP_TASK_WDT_INIT=n` (ou `CONFIG_ESP_TASK_WDT=n`) dans le build, soit une autre chaîne fournit encore le TWDT (ex. framework-espidf), soit le link utilise un autre jeu de .a. Voir aussi le correctif « early init » ci‑dessous et le script `tools/patch_arduino_early_init_variant.py`.

### Correctif « early init » (optionnel)

Pour désactiver le TWDT **avant** `nvs_flash_init()` : exécuter `python tools/patch_arduino_early_init_variant.py`, qui patch le core Arduino pour appeler `earlyInitVariant()` au tout début de `initArduino()`. Le projet définit `earlyInitVariant()` dans `app.cpp` (S3) pour appeler `esp_task_wdt_deinit()`. À réappliquer après mise à jour du package framework Arduino.

## Si le boot loop persiste
Le firmware utilise encore la lib précompilée (300 ms). Vérifier que le build affiche bien `*** Compile Arduino IDF libs for wroom-s3-test ***` et `Replace:` ou `Add: # CONFIG_ESP_INT_WDT is not set`. Si absent, le chemin HandleArduinoIDFsettings ne s’exécute pas.
