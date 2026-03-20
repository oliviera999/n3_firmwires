# Documentation FFP5CS

Documentation technique du projet ESP32 Aquaponie Controller.

## Structure du projet

- `src/` — Code source C++ (implémentation)
- `include/` — Headers C++ (interfaces)
- `data/` — Fichiers web (HTML, CSS, JS)
- `test/` — Tests unitaires (framework Unity, env `native`). Exécution : `pio test -e native` ou `.\scripts\test_unit_all.ps1`. Seuls `test_nvs` et `test_config` sont exécutés (voir `platformio.ini` env `native`, `test_filter`).

## Structure de la documentation

```
docs/
├── README.md           # Ce fichier
├── technical/          # Références techniques
│   ├── VARIABLE_NAMING.md        # Contrat nommage (NVS, API, serveur, firmware)
│   └── SEUILS_SERVEUR_ESP32.md   # Seuils ESP32 vs serveur PHP
├── reports/            # Rapports et analyses
│   ├── analysis/       # Qualité, conformité, NVS
│   ├── corrections/    # Résumé des corrections appliquées
│   └── monitoring/     # Origine des problèmes (DHT22, heap, etc.)
└── references          # Référence rapide emails (32 types, conditions d'envoi)
```

### Liens utiles

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
| **wroom-test** * | esp32dev | pioarduino 55.03.37 | test | DHT11 | /ffp3/post-data-test | wroom_test | OLED diag, endpoints dangereux |
| **wroom-s3-test** * | esp32-s3-devkitc-1 | espressif32 6.13.0 | test | BME280/DHT auto | /ffp3/post-data3-test | s3_test | RTC DS3231, OLED diag |
| **wroom-s3-prod** * | esp32-s3-devkitc-1 | espressif32 6.13.0 | prod | BME280/DHT auto | /ffp3/post-data3 | s3_test | Serveur web désactivé, serial off |
| wroom-beta | esp32dev | pioarduino 55.03.37 | beta | DHT11 | /ffp3/post-data-test | wroom_ota_fs_mail | Reflet prod, entêtes beta |
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

# Environnement production
pio run -e wroom-prod

# Flash
pio run -e wroom-test -t upload

# Monitor série
pio device monitor
```

**Basculement WROOM ↔ S3** : après un build d’une autre famille d’env (ex. wroom-test puis wroom-s3-test), il est recommandé de lancer `pio run -e <env_cible> -t clean` avant de compiler. Le script `build_all_envs.ps1` fait ce nettoyage automatiquement lors du basculement de famille. **wroom-beta** : si le build échoue (FRAMEWORK_DIR None), lancer d’abord `pio run -e wroom-prod` avec succès, puis `pio run -e wroom-beta`. Détails : [BUILD_S3_PROCESS_ANALYSE.md](technical/BUILD_S3_PROCESS_ANALYSE.md) (sections « Basculement WROOM ↔ S3 » et « wroom-beta et FRAMEWORK_DIR »).

## Principes de développement

Voir les règles du projet dans `.cursor/rules/` (ex. règles cœur FFP5CS).

**Principes clés :**
1. Offline-first : le système fonctionne sans réseau
2. Simplicité : éviter la sur-ingénierie
3. Robustesse : ne jamais crasher
4. Autonomie : configuration locale prioritaire
