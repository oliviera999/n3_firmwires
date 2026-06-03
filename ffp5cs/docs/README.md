# Documentation FFP5CS

Documentation technique du projet ESP32 Aquaponie Controller.

## Structure du projet

- `src/` — Code source C++ (implémentation)
- `include/` — Headers C++ (interfaces)
- `data/` — Fichiers web (HTML, CSS, JS)
- `test/` — Tests unitaires (framework Unity, env `native`). Exécution : `pio test -e native` ou `.\scripts\test_unit_all.ps1`. Les suites principales couvrent `test_config`, `test_nvs`, `test_server_url` et `test_sensor_validation`.

## Structure de la documentation

```
docs/
├── README.md           # Ce fichier
├── technical/          # Références techniques
│   ├── COMPILATION_WROOM_PIOARDUINO_ET_ENVS.md  # Build WROOM, pioarduino 2 passes, vs n3pp/msp, flash
│   ├── WROOM_BETA_LOCAL_BUILD_FLASH_TEST.md     # wroom-beta-local : build, flash COMx, tests Docker
│   ├── BUILD_S3_PROCESS_ANALYSE.md              # Build S3 multi-phases, bascule WROOM↔S3
│   ├── VARIABLE_NAMING.md        # Contrat nommage (NVS, API, serveur, firmware)
│   └── SEUILS_SERVEUR_ESP32.md   # Seuils ESP32 vs serveur PHP
├── reports/            # Rapports et analyses
│   ├── analysis/       # Qualité, conformité, NVS
│   ├── corrections/    # Résumé des corrections appliquées
│   └── monitoring/     # Origine des problèmes (DHT22, heap, etc.)
└── references          # Référence rapide emails (32 types, conditions d'envoi)
```

### Liens utiles

- **[Compilation WROOM / pioarduino / envs](technical/COMPILATION_WROOM_PIOARDUINO_ET_ENVS.md)** — tutoriel, comparaison avec n3pp/msp/cam, `wroom-prod-pio6`, erreurs fréquentes, flash cohérent
- **[wroom-beta-local — build, flash, tests](technical/WROOM_BETA_LOCAL_BUILD_FLASH_TEST.md)** — warmup n3pp, CP210x, Docker LAN, suites de tests
- **[Convention nommage / contrat](technical/VARIABLE_NAMING.md)** — NVS, API locale, serveur distant, firmware (source : `include/gpio_mapping.h`, `include/nvs_keys.h`)
- **[Seuils ESP32 / serveur](technical/SEUILS_SERVEUR_ESP32.md)** — Différences volontaires (température, humidité, etc.)
- **[Référence matériel ESP32-S3](technical/ESP32S3_HARDWARE_REFERENCE.md)** — Modèle N16R8, envs S3, boot PSRAM (TG1WDT), **comportement firmware S3 PSRAM** (Serial/CDC, priorités tâches, OLED, recommandations)
- **`references`** — Liste des 32 types d’emails, quotas, optimisations
- **Rapports** :
  - [Origine problèmes critiques](reports/monitoring/reports/ANALYSE_ORIGINE_PROBLEMES_CRITIQUES.md) — DHT22, watchdog, mémoire, boucles de reboot
  - [Résumé corrections](reports/corrections/RESUME_CORRECTIONS_APPLIQUEES.md) — Incohérences corrigées (2026-01-21)
  - [Conformité .cursorrules](reports/analysis/compliance/RAPPORT_CONFORMITE_CURSORRULES_POST_CORRECTIONS.md) — État actuel (~92 %)
  - [Système NVS](reports/analysis/code-quality/RAPPORT_ANALYSE_SYSTEME_NVS.md) — Architecture, namespaces, usage
  - [Code mort](reports/analysis/code-quality/RAPPORT_VERIFICATION_CODE_MORT_FINALE_2026-01-25.md) — Vérification post-nettoyage
  - [Optimisations](reports/analysis/code-quality/RAPPORT_OPTIMISATIONS_PERFORMANCE.md) — NVS, String, simplicité

## Configuration

Toute la configuration est centralisée dans `include/config.h`.

## Matrice des environnements

Les **4 environnements critiques** (qui doivent compiler sans erreur avant chaque livraison) sont marqués d'un astérisque.

| Env | Board | Plateforme | Profil | Capteur air | Endpoints serveur | Partition | Notes |
|-----|-------|-----------|--------|-------------|-------------------|-----------|-------|
| **wroom-prod** * | esp32dev | pioarduino 55.03.37 | prod | DHT22 | /ffp3/post-data | wroom_ota_fs_mail | Serveur web désactivé, serial off |
| **wroom-prod-pio6** | esp32dev | espressif32 6.13.0 | prod | DHT22 | /ffp3/post-data | wroom_ota_fs_mail | Secours build 1 passe (Arduino 2.x) — voir [guide compilation](technical/COMPILATION_WROOM_PIOARDUINO_ET_ENVS.md) |
| **wroom-test** * | esp32dev | pioarduino 55.03.37 | test | DHT11 | /ffp3/post-data-test | wroom_test | OLED diag, endpoints dangereux |
| **wroom-s3-test** * | esp32-s3-devkitc-1 | espressif32 6.13.0 | test | BME280/DHT auto | /ffp3/post-data3-test | s3_test | RTC DS3231, OLED diag |
| **wroom-s3-prod** * | esp32-s3-devkitc-1 | espressif32 6.13.0 | prod | BME280/DHT auto | /ffp3/post-data3 | s3_test | Serveur web désactivé, serial off |
| wroom-beta | esp32dev | pioarduino 55.03.37 | beta | DHT11 | /ffp3/post-data-test | wroom_ota_fs_mail | Reflet prod, entêtes beta |
| wroom-beta-local | esp32dev | pioarduino 55.03.37 | beta | DHT11 | local `/ffp3/post-data-test` | wroom_ota_fs_mail | Clone beta avec `USE_LOCAL_SERVER_ENDPOINTS` + override local |
| wroom-s3-test-psram | esp32-s3-devkitc-1 | espressif32 6.13.0 | test | BME280/DHT auto | /ffp3/post-data3-test | s3_test | N16R8 PSRAM, patches Arduino |
| wroom-s3-test-psram-v2 | esp32-s3-devkitc-1 | espressif32 6.13.0 | test | BME280/DHT auto | /ffp3/post-data3-test | s3_test | PSRAM sans patches |
| wroom-s3-test-devkit | esp32-s3-devkitc-1 | espressif32 6.13.0 | test | BME280/DHT auto | /ffp3/post-data3-test | s3_8mb | DevKitC-1 8 Mo flash |
| wroom-s3-test-usb | esp32-s3-devkitc-1 | espressif32 6.13.0 | test | BME280/DHT auto | /ffp3/post-data3-test | s3_test | USB Serial/JTAG natif |
| wroom-tls-test | esp32dev | espressif32 6.13.0 | test | — | — | wroom_test | Test TLS minimal (fichier unique) |

### Vérification multi-env

```powershell
# Compiler les 4 envs critiques
.\scripts\build_all_envs.ps1

# CI complète (build multi-env + analyse log)
.\scripts\run_ci_checks.ps1 -AllEnvs
```

## Compilation

```bash
# Environnement test
pio run -e wroom-test

# Environnement production (pioarduino, 2 passes — peut prendre 10–15 min au 1er build)
pio run -e wroom-prod

# Secours si phase 2 pioarduino échoue (build classique ~1–2 min)
pio run -e wroom-prod-pio6

# Flash
pio run -e wroom-test -t upload

# Monitor série
pio device monitor
```

**Guide détaillé** (éviter les échecs, tailles de `firmware.bin`, flash homogène, comparaison n3pp/msp) : **[technical/COMPILATION_WROOM_PIOARDUINO_ET_ENVS.md](technical/COMPILATION_WROOM_PIOARDUINO_ET_ENVS.md)**.

### `wroom-beta-local` (build, flash, tests Docker)

Guide pas à pas : **[technical/WROOM_BETA_LOCAL_BUILD_FLASH_TEST.md](technical/WROOM_BETA_LOCAL_BUILD_FLASH_TEST.md)**.

Résumé :

1. `include/secrets.h` + `include/local_server_overrides.h` (IP LAN Docker, ex. `http://192.168.0.158:8082`).
2. Warmup pioarduino si besoin : `cd firmwires\n3pp` → `pio run -e esp32dev`, puis `pio run -e wroom-beta-local`.
3. Valider `.pio\build\wroom-beta-local\firmware.bin` (~1,55–1,65 Mo) et `version.txt`.
4. Flash : `pio run -e wroom-beta-local -t upload --upload-port COM5` ou esptool (CP210x : BOOT+RST, voir guide §3.3).
5. Stack : `serveur\tools\local-docker.ps1 up` puis `.\scripts\run_wroom_beta_local_test_suite.ps1 -Port COM5 -Campaign quick -Auth both`.
6. **Panneau ↔ ESP (bidirectionnel)** : `.\scripts\test_bidirectional_control_panel_local.ps1 -Port COM5` — rapport dans `logs/bidirectional_control_*.md`.

Script rapide : `.\build_upload_monitor_wroom_beta_local.ps1 -Port COM5` (raccourci COM4 : `build_upload_monitor_wroom_beta_local_com4.ps1`).

## Workflow de validation recommandé

- Workflow complet (erase + flash + monitor + analyse) :
  - `.\erase_flash_fs_monitor_5min_analyze.ps1 -Environment wroom-test -Port COM3`
- Monitoring jusqu'au crash/reboot :
  - `.\monitor_until_crash.ps1 -Port COM3 -PostRebootSeconds 60 -MaxWaitSeconds 3600`
- Build multi-env critiques :
  - `.\scripts\build_all_envs.ps1`
- Build multi-env + beta-local :
  - `.\scripts\build_all_envs.ps1 -IncludeBetaLocal`
- Test cible beta-local (option 3) :
  - `.\scripts\test_wroom_beta_local_serial.ps1 -Port COM7 -MonitorSeconds 150`
- Test integration beta-local Docker + appareil (option 5) :
  - `.\scripts\test_wroom_beta_local_docker_integration.ps1 -Port COM7 -AuthMode both`
- Batterie quick/full beta-local (token/session) :
  - `.\scripts\run_wroom_beta_local_test_suite.ps1 -Port COM7 -Campaign quick -Auth both`
  - `.\scripts\run_wroom_beta_local_test_suite.ps1 -Port COM7 -Campaign full -Auth both`
- Secrets locaux batterie :
  - copier `scripts/.beta-local-test.env.example` vers `scripts/.beta-local-test.env` (fichier ignore par Git).

**Basculement WROOM ↔ S3** : après un build d’une autre famille d’env (ex. wroom-test puis wroom-s3-test), il est recommandé de lancer `pio run -e <env_cible> -t clean` avant de compiler. Le script `build_all_envs.ps1` fait ce nettoyage automatiquement lors du basculement de famille. **wroom-beta** : si le build échoue (FRAMEWORK_DIR None), lancer d’abord `pio run -e wroom-prod` avec succès, puis `pio run -e wroom-beta`. Détails WROOM : [COMPILATION_WROOM_PIOARDUINO_ET_ENVS.md](technical/COMPILATION_WROOM_PIOARDUINO_ET_ENVS.md) ; détails S3 : [BUILD_S3_PROCESS_ANALYSE.md](technical/BUILD_S3_PROCESS_ANALYSE.md).

## Principes de développement

Voir les règles du projet dans `.cursor/rules/` (ex. règles cœur FFP5CS).

**Principes clés :**
1. Offline-first : le système fonctionne sans réseau
2. Simplicité : éviter la sur-ingénierie
3. Robustesse : ne jamais crasher
4. Autonomie : configuration locale prioritaire
