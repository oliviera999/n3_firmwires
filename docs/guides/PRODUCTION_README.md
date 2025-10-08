# État de Production - ESP32 FFP3CS4

## Configuration Actuelle

Le projet est maintenant configuré pour la **production** avec les optimisations suivantes :

### Flags de Build de Production
- `CORE_DEBUG_LEVEL=1` : Niveau de debug minimal pour les performances
- `LOG_LEVEL=LOG_INFO` : Logs informatifs uniquement
- `NDEBUG` : Désactive les assertions de debug
- Optimisations pour la stabilité et les performances

### Environnements de Test
Tous les environnements de test ont été **commentés** dans `platformio.ini` pour éviter les interférences :
- `[env:test-serial]` : Commenté
- `[env:test-logic]` : Commenté

## Organisation des Tests

### Tests Archivés
Tous les fichiers de test ont été déplacés dans le répertoire `tests_archive/` :

#### Scripts Python de Test
- `test_connectivity.py` - Tests de connectivité
- `test_oled_optimization.py` - Tests d'optimisation OLED
- `test_display_task.py` - Tests des tâches d'affichage
- `test_countdown_fluidity.py` - Tests de fluidité du compte à rebours
- `test_ota_flag.py` - Tests des flags OTA
- `test_ota_debug.py` - Tests de debug OTA
- `test_nvs_optimization.py` - Tests d'optimisation NVS
- `test_wakeup_timing.py` - Tests de timing de réveil
- `test_wakeup_persistence.py` - Tests de persistance du réveil
- `test_heating_commands.py` - Tests des commandes de chauffage
- `test_serial_automated.py` - Tests série automatisés
- `test_simulation.py` - Tests de simulation
- `run_all_tests.py` - Script principal de tests
- `test_summary.py` - Résumé des tests
- `test_final_report.py` - Rapport final des tests

#### Tests C++
- `simple_test.cpp` - Tests simples
- Fichiers de backup (.bak) - Sauvegardes des versions précédentes

#### Tests PHP
- `ffp3-outputs-action2.php` - Tests d'actions de sortie
- `post-ffp3-data2.php` - Tests de post de données
- `heartbeat.php` - Tests de heartbeat

## Compilation et Upload

### Pour la Production
```bash
pio run -e esp32-s3-devkitc-1-n16r8v
pio run -e esp32-s3-devkitc-1-n16r8v -t upload
```

### Pour Activer les Tests (si nécessaire)
1. Décommenter les environnements de test dans `platformio.ini`
2. Copier les fichiers nécessaires depuis `tests_archive/`
3. Recompiler avec l'environnement de test souhaité

## Monitoring de Production

### Logs
- Niveau de debug : `CORE_DEBUG_LEVEL=1`
- Niveau de log : `LOG_INFO`
- Les logs détaillés sont désactivés pour les performances

### Performance
- Optimisations activées avec `-DNDEBUG`
- Debug désactivé pour maximiser les performances
- Configuration stable pour un fonctionnement 24/7

## Maintenance

### Pour Ajouter de Nouveaux Tests
1. Créer les fichiers dans `tests_archive/`
2. Documenter dans ce README
3. Ne pas modifier la configuration de production

### Pour Modifier la Configuration de Production
1. Modifier uniquement l'environnement `esp32-s3-devkitc-1-n16r8v`
2. Tester en environnement de développement d'abord
3. Documenter les changements

## Sécurité
- Les tests ne peuvent plus interférer avec la production
- Configuration isolée et stable
- Logs de production optimisés pour la surveillance 