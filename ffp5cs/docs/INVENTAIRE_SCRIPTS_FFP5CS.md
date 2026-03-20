# Inventaire des scripts monitoring, flash et autres – FFP5CS

**Date :** 2026-03-08  
**Périmètre :** Dossier `ffp5cs` (racine, `scripts/`, `tools/`). Exclus : `managed_components/`.

---

## 1. Scripts de monitoring (série / logs)

| Fichier | Type | Rôle |
|---------|------|------|
| **monitor_5min.ps1** | PS1 | Monitoring N minutes (défaut 5 min), option port + env. Logs et analyses dans le dossier dédié **logs/** (plus à la racine). |
| **monitor_until_reboot.ps1** | PS1 | Monitoring jusqu’à détection d’un reboot (sans flash). |
| **monitor_until_crash.ps1** | PS1 | (Fichier vide dans l’état actuel.) |
| **monitor_long_run.ps1** | PS1 | Monitoring longue durée. |
| **monitor_log_wroom_test.ps1** | PS1 | Monitoring avec enregistrement, durée paramétrable (défaut 3 min). |
| **check_monitoring.ps1** | PS1 | État du monitoring (processus Python, dernier log, recherche de crash). |
| **check_monitoring_status.ps1** | PS1 | Vérification périodique du monitoring (fichiers monitor_until_crash*, processus Python). |
| **wait_and_analyze_monitoring.ps1** | PS1 | Attendre la fin d’un monitoring (fichier stable) puis lancer l’analyse. |
| **aggregate_monitor_report.ps1** | PS1 | Agrégation de rapports de monitoring. |
| **tools/monitor/simple_capture.ps1** | PS1 | Capture simple du moniteur série. |
| **tools/monitor/capture_logs.ps1** | PS1 | Capture des logs du moniteur. |
| **tools/monitor/capture_boot.py** | Python | Capture des logs de boot (DTR/RTS reset, lecture série durée configurable). |
| **tools/monitor/monitor_until_crash.py** | Python | Monitoring jusqu’à crash (Python). |
| **tools/monitor/monitor_unlimited.py** | Python | Monitoring illimité (série). |
| **tools/monitor/monitor_ota.py** | Python | Monitoring pendant/suite OTA. |
| **tools/monitor/monitor_summary.py** | Python | Résumé des sessions de monitoring. |
| **tools/monitor/serial_utils.py** | Python | Utilitaires série (ouverture, libération port). |
| **tools/monitor/serial_capture.py** | Python | Capture série. |
| **.cursor/capture_monitor.ps1** | PS1 | Capture moniteur (usage Cursor / règles). |

---

## 2. Scripts de flash et erase

| Fichier | Type | Rôle |
|---------|------|------|
| | **flash_wroom_test.ps1** | PS1 | Flash seul (firmware + LittleFS) env wroom-test, sans monitoring. |
| **flash_s3_test.ps1** | PS1 | Flash seul (firmware + LittleFS) env wroom-s3-test (ESP32-S3), sans monitoring. | |
| **flash_and_monitor_until_reboot.ps1** | PS1 | Flash + monitoring jusqu’à reboot + analyse. |
| **flash_and_monitor_10min_wroom_test.ps1** | PS1 | Flash + monitoring 10 min + analyse (validation 24/7). |
| **tools/erase_flash.ps1** | PS1 | Efface la flash via PlatformIO (port + env paramétrables). |
| **tools/flash/flash_esp32.py** | Python | Flash ESP32-WROOM (clean, build, upload) configuration FFP3. |
| **tools/flash/deploy_ota.py** | Python | Déploiement OTA moderne (compile, flash, tests). |
| **tools/recovery/reset_flash.py** | Python | Reset flash : complet (flash+NVS+OTA), NVS seul, OTA seul, ou FS. |
| **tools/recovery/quick_reset.py** | Python | Reset rapide. |
| **tools/recovery/emergency_recovery.py** | Python | Récupération d’urgence. |
| **tools/force_ota_partition.py** | Python | Forcer la partition OTA. |

---

## 3. Workflows combinés (erase + flash + monitor + analyse)

| Fichier | Type | Rôle |
|---------|------|------|
| **erase_flash_fs_monitor_5min_analyze.ps1** | PS1 | Workflow complet : erase → flash (firmware + FS sauf wroom-prod) → monitor N min → analyse. Tous les logs et rapports sont écrits dans **logs/**. Référence recommandée. S'applique aussi à l'ESP32-S3 avec `-Environment wroom-s3-test` ou `-Environment wroom-s3-prod`. Option **-NoPrompt** : exécution non interactive (pas d'attente Entrée avant erase/flash). |
| **run_wroom_s3_prod_workflow.ps1** | PS1 | Workflow wroom-s3-prod : erase + flash + LittleFS + monitor 2 min + analyse. |
| **run_s3_validation.ps1** | PS1 | Build wroom-s3-test puis erase + flash + monitor 1 min. |
| **run_s3_psram_validation.ps1** | PS1 | Validation S3 PSRAM (build + workflow). |
| **test_flash_and_monitor_script.ps1** | PS1 | Test de cohérence du script flash_and_monitor_until_reboot (syntaxe, sections). |

---

## 4. Analyse des logs et diagnostic

| Fichier | Type | Rôle |
|---------|------|------|
| **analyze_log.ps1** | PS1 | Analyse générique (patterns critiques, POST, JSON, heap, DHT22). |
| **analyze_log_exhaustive.ps1** | PS1 | Analyse exhaustive (crashes, watchdog, mémoire, réseau, NVS, reboots). |
| **decode_backtrace.ps1** | PS1 | Décodage backtrace (Guru Meditation) via addr2line ; auto-sélection dernier monitor_5min_*.log si besoin. |
| **diagnostic_communication_serveur.ps1** | PS1 | Diagnostic communication serveur (envoi/réception, [Sync]). |
| **diagnostic_serveur_distant.ps1** | PS1 | Analyse POST/GET/Heartbeat, cohérence avec le code. |
| **generate_diagnostic_report.ps1** | PS1 | Agrège serveur + mails + analyse exhaustive → rapport Markdown. |
| **check_emails_from_log.ps1** | PS1 | Vérification mails (attendus vs envoyés/échoués) depuis un log. |
| **cleanup_old_logs.ps1** | PS1 | Nettoyage des logs obsolètes dans le dossier **logs/** (vides, .errors orphelins, gros fichiers, anciens par type). |
| **tools/monitor/analyze_boot_log.py** | Python | Analyse des logs de boot. |
| **tools/monitor/analyze_remote_server_exchanges.py** | Python | Analyse des échanges avec le serveur distant. |
| **tools/monitor/diagnostic_analyzer.py** | Python | Analyseur de diagnostic. |

---

## 5. Coredump (extraction et analyse)

| Fichier | Type | Rôle |
|---------|------|------|
| **tools/coredump/coredump_tool.py** | Python | Outil principal coredump. |
| **tools/coredump/extract_coredump.py** | Python | Extraction du coredump. |
| **tools/coredump/extract_elf_from_coredump.py** | Python | Extraction ELF depuis le coredump. |
| **tools/coredump/analyze_coredump.py** | Python | Analyse du coredump. |
| **tools/coredump/analyze_coredump_detailed.py** | Python | Analyse détaillée. |
| **tools/coredump/analyze_elf_coredump.py** | Python | Analyse ELF + coredump. |
| **tools/coredump/compare_coredumps.py** | Python | Comparaison de coredumps. |
| **tools/coredump/temp_analyze.py** | Python | Analyse temporaire / debug. |

---

## 6. Build, OTA, production

| Fichier | Type | Rôle |
|---------|------|------|
| **scripts/publish_ota.ps1** | PS1 | Délègue au script racine IOT_n3 : publication vers **serveur/ota/** (dépôt n3_serveur), URLs **/ota/**. |
| **scripts/build_production.ps1** | PS1 | Build production (minify assets, compile, optionnel upload firmware/FS). |
| **scripts/verify_version.ps1** | PS1 | Vérification de la version (config.h, etc.). |
| **tools/pio_write_build_version.py** | Python | Écrit la version de build (hook post-build PlatformIO). |
| **tools/calculate_md5.py** | Python | Calcul MD5 (firmware/binaries). |
| **tools/run_build_s3_capture.py** | Python | Build S3 + capture. |

---

## 7. Scripts divers (CI, tests, utilitaires)

| Fichier | Type | Rôle |
|---------|------|------|
| **scripts/run_ci_checks.ps1** | PS1 | Verifications CI : build (env defaut ou multi-env avec `-AllEnvs`) + analyse log serie. |
| **scripts/test_unit_all.ps1** | PS1 | Tests unitaires : `pio test -c platformio-native.ini -e native`. |
| **scripts/build_all_envs.ps1** | PS1 | Build multi-env : compile les 4 envs critiques (wroom-prod, wroom-test, wroom-s3-test, wroom-s3-prod) avec nettoyage auto au basculement WROOM/S3. Options : `-Clean`, `-StopOnError`, `-Verbose`, `-Envs`. |
| **scripts/test_phase2_complete.ps1** | PS1 | Test phase 2 complète (NVS, etc.). |
| **scripts/test_web_dedicated_architecture.sh** | Shell | Test architecture web dédiée. |
| **scripts/Release-ComPort.ps1** | PS1 | Libération du port COM (arrêt processus moniteur). |
| **scripts/enable_long_paths.ps1** | PS1 | Activation des chemins longs (Windows). |
| **scripts/minify_assets.py** | Python | Minification des assets web. |
| **restart_esp32.ps1** | PS1 | Reset ESP32 via esptool + lancement du monitor. |
| **sync_all.ps1** | PS1 | Mise à jour de tous les sous-modules + commit si changements. |
| **sync_ffp3distant.ps1** | PS1 | Sous-module ffp3distant (update/push/pull/status). |
| **install_cppcheck.ps1** | PS1 | Installation de cppcheck (Chocolatey/winget/manuel). |
| **_add_bom_all.ps1** / **_add_bom.ps1** | PS1 | Ajout BOM UTF-8 aux fichiers. |
| **read_current_logs.py** | Python | Lecture des logs courants. |
| **test_gpio_sync.py** | Python | Test GPIO / synchronisation. |
| **tools/test_time_drift.py** | Python | Test dérive temporelle. |
| **tools/configure_time_drift.py** | Python | Configuration dérive temporelle. |
| **tools/verify_wroom_wdt_sdkconfig.ps1** | PS1 | Vérification sdkconfig WDT WROOM. |
| **tools/clean_s3_build.ps1** | PS1 | Nettoyage build S3. |

---

## 8. Patches et build (PlatformIO / S3 / Arduino)

Scripts appelés par `platformio.ini` ou utilisés pour patcher l’environnement :

| Fichier | Type | Rôle |
|---------|------|------|
| **tools/pio_ensure_secrets.py** | Python | Vérification des secrets (pre-build). |
| **tools/pio_add_mklittlefs_path.py** | Python | Ajout du chemin mklittlefs (pre-build). |
| **tools/pio_ensure_lib_dirs_windows.py** | Python | Pré-création des répertoires de build des libs à noms avec espaces (Windows, env S3 PSRAM). Évite « opening dependency file … No such file or directory ». |
| **tools/pio_write_build_version.py** | Python | Écriture version de build (post-build). |
| **tools/pio_s3_psram_patch_iwdt.py** | Python | Patch IWDT pour S3 PSRAM. |
| **tools/pio_pre_apply_arduino_patches.py** | Python | Application des patches Arduino (pre). |
| **tools/patch_arduino_diag_after_nvs.py** | Python | Patch Arduino diagnostic après NVS. |
| **tools/patch_arduino_libs_s3_wdt.py** | Python | Patch libs Arduino S3 WDT. |
| **tools/patch_arduino_early_init_variant.py** | Python | Patch early init variant Arduino. |
| **tools/patch_arduino_app_main_early_wdt.py** | Python | Patch app_main early WDT. |
| **tools/s3_disable_esp_insights.py** | Python | Désactivation ESP Insights pour S3. |
| **tools/s3_patch_platform_espidf_ldgen.py** | Python | Patch ldgen ESP-IDF S3. |
| **tools/s3_patch_ldgen_fragments_file.py** | Python | Patch fragments ldgen S3. |
| **tools/s3_patch_wpa_supplicant_wno_error.py** | Python | Patch WPA supplicant S3. |
| **tools/s3_patch_esp_insights_mqtt.py** | Python | Patch ESP Insights MQTT S3. |
| **tools/s3_idf_git_data_fix.py** | Python | Correctif git data IDF S3. |
| **tools/patch_espressif32_custom_sdkconfig_only.py** | Python | Patch sdkconfig custom espressif32. |
| **tools/static_asset_probe.py** | Python | Sondage des assets statiques. |

---

## 9. Références

- **Workflow recommandé (règles projet) :** `erase_flash_fs_monitor_5min_analyze.ps1` pour erase → flash → monitor → analyse. Pour **WROOM** : `-Environment wroom-test` (défaut). Pour **ESP32-S3** : `-Environment wroom-s3-test` ou `-Environment wroom-s3-prod`. Exemple : `.\erase_flash_fs_monitor_5min_analyze.ps1 -Environment wroom-s3-test -Port COM7`.
- **Scripts dédiés S3 :** `run_s3_validation.ps1` (wroom-s3-test, 1 min), `run_wroom_s3_prod_workflow.ps1` (wroom-s3-prod, 2 min), `flash_s3_test.ps1` (flash seul wroom-s3-test).
- **Publication OTA :** `scripts/publish_ota.ps1` ; voir `docs/technical/OTA_PUBLISH.md`.
- **Analyse détaillée des scripts racine :** `ANALYSE_SCRIPTS_PS1_RACINE.md` (recommandations garder/supprimer/fusionner, corrections).

---

*Inventaire généré pour le projet FFP5CS – firmware n3 IoT.*
