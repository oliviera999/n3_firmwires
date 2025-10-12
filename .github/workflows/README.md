# GitHub Actions CI/CD

## Workflow de build

Le workflow `build.yml` compile automatiquement le projet ESP32 sur chaque push ou pull request.

### Environnements testés

- **wroom-prod** : Version de production pour ESP32-WROOM-32
- **wroom-dev** : Version de développement avec debug activé

### Corrections apportées

#### Problème initial (Exit code 1 & 128)

Le build échouait avec deux erreurs principales :

1. **Exit code 1** : Utilisation d'un environnement PlatformIO inexistant (`esp32dev`)
   - **Solution** : Utilisation des environnements valides définis dans `platformio.ini` (wroom-prod, wroom-dev, etc.)

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
- `firmware-wroom-dev/firmware.bin` : Firmware de développement
- `littlefs-wroom-prod/littlefs.bin` : Système de fichiers (si disponible)

### Commandes locales utiles

```bash
# Compiler pour production
pio run --environment wroom-prod

# Compiler pour développement
pio run --environment wroom-dev

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

Le workflow vérifie automatiquement que la version a été incrémentée dans `src/config.h` ou `src/main.cpp`.

**Rappel** : Toujours incrémenter la version avant de push !
- **MINOR** (+0.01) : Corrections, optimisations mineures
- **MAJOR** (+1.00) : Nouvelles fonctionnalités majeures

---

**Dernière mise à jour** : 2025-10-12  
**Status** : ✅ Opérationnel

