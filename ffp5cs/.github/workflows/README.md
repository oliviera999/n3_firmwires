# GitHub Actions CI/CD

## Workflow de build

Le workflow `build.yml` compile automatiquement le projet ESP32 sur chaque push ou pull request.

### Environnements testés

- **wroom-prod** : Version de production pour ESP32-WROOM-32
- **wroom-test** : Version de test avec debug activé (coredump, endpoints diagnostic)

**ESP32-S3 (wroom-s3-test, wroom-s3-prod)** : Les builds S3 ne sont pas exécutés en CI (patches et toolchain spécifiques, build long). Validation manuelle recommandée :
- **Build + flash + monitor 1 min** : `.\run_s3_validation.ps1 [-Port COMx]`
- **Workflow complet** : `.\erase_flash_fs_monitor_5min_analyze.ps1 -Environment wroom-s3-test [-Port COMx]`

### Corrections apportées

#### Problème initial (Exit code 1 & 128)

Le build échouait avec deux erreurs principales :

1. **Exit code 1** : Utilisation d'un environnement PlatformIO inexistant (`esp32dev`)
   - **Solution** : Utilisation des environnements valides définis dans `platformio.ini` (wroom-prod, wroom-test, etc.)

2. **Exit code 128** : Erreur git avec les sous-modules
   - **Cause** : `.gitmodules` référençait `ffp3distant` qui n'existe plus
   - **Solution** : Mise à jour de `.gitmodules` pour référencer correctement `ffp3`

#### Optimisations

- **Cache PlatformIO** : Réduction du temps de build de ~2 minutes
- **Cache pip** : Accélération de l'installation des dépendances
- **Artifacts** : Conservation des firmwares compilés pendant 30 jours
- **Matrix build** : Test de plusieurs environnements en parallèle

### Utilisation

Le workflow se déclenche automatiquement sur :
- Push sur `main` ou `develop`
- Pull request vers `main`

### Artifacts générés

Après chaque build réussi, les artifacts suivants sont disponibles :
- `firmware-wroom-prod/firmware.bin` : Firmware de production
- `firmware-wroom-prod/firmware.elf` : Fichier ELF pour debug
- `firmware-wroom-test/firmware.bin` : Firmware de test
- `littlefs-wroom-prod/littlefs.bin` : Système de fichiers (si disponible)

### Commandes locales utiles

```bash
# Compiler pour production
pio run --environment wroom-prod

# Compiler pour test (debug activé)
pio run --environment wroom-test

# Vérifier la taille du firmware
pio run --environment wroom-prod --target size

# Lister tous les environnements disponibles
pio project config --json-output
```

### Troubleshooting

#### Le build échoue avec "Unknown environment"

Vérifiez que l'environnement existe dans `platformio.ini` :
```bash
pio project config
```

#### Erreurs git avec les sous-modules

Synchronisez les sous-modules :
```bash
git submodule sync
git submodule update --init --recursive
```

#### Le firmware est trop gros (> 100% Flash)

1. Vérifiez la taille actuelle : `pio run -e wroom-prod --target size`
2. Réduisez la taille en :
   - Désactivant les logs debug en production
   - Compressant les assets web
   - Utilisant une partition optimisée

### Monitoring post-déploiement

Conformément aux règles du projet, après chaque déploiement réussi :

1. **Monitoring de 90 secondes** obligatoire
2. Analyse des logs pour identifier :
   - 🔴 Erreurs critiques (Guru Meditation, Panic, Brownout)
   - 🟡 Warnings (watchdog, timeouts)
   - 🟢 Utilisation mémoire (heap/stack)
3. Documentation des problèmes trouvés

### Versions

La version firmware est définie dans **`include/config.h`** (`ProjectConfig::VERSION`). C’est la source de vérité unique (API `/version`, OTA, sync serveur).

**Vérification automatique** : Sur chaque PR vers `main` et chaque push direct sur `main`, le job `version-check` vérifie que la version a été incrémentée par rapport à la base. Si la version est identique ou inférieure, le CI échoue.

**Rappel** : Incrémenter la version dans `include/config.h` avant de créer/merge une PR, et créer un tag Git pour les releases (`git tag v11.xxx`).

- **MINOR** (+0.01) : Corrections, optimisations mineures
- **MAJOR** (+1.00) : Nouvelles fonctionnalités majeures

---

**Dernière mise à jour** : 2026-01-31  
**Status** : ✅ Opérationnel

