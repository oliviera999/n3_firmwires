# Tests Unitaires - FFP5CS

Ce dossier contient les tests unitaires du projet FFP5CS utilisant le framework Unity intégré à PlatformIO.

## 🚀 Démarrage Rapide

### Exécuter tous les tests

```bash
pio test -e native
```

### Script PowerShell

```powershell
.\scripts\test_unit_all.ps1
```

## 📁 Structure

- `test_mocks.h` — Mocks pour fonctions Arduino/ESP32 (millis, Serial, etc.)
- `unity_config.h` — Configuration Unity pour tests natifs
- `README.md` — Ce fichier

Suites exécutées par l’env `native` (voir `test_filter` dans `platformio.ini`) :
- `../test_nvs/` — Validation NVS (clés, mock)
- `../test_config/` — ConfigManager (mock)

La suite `test_rate_limiter` est exclue (dépend de `rate_limiter.h` absent du projet).

## 📚 Documentation

- **Documentation générale**: [docs/README.md](../../docs/README.md)
- Les commandes et la structure des tests sont décrites ci‑dessous et dans la checklist.

## ✅ Checklist pour Nouveau Test

1. Créer `test_mon_module.cpp` dans ce dossier
2. Inclure `test_mocks.h` si nécessaire
3. Implémenter `setUp()` et `tearDown()`
4. Écrire les tests avec pattern AAA
5. Compiler: `pio test -e native -f test_mon_module`
6. Vérifier que tous les tests passent

## 🔍 Commandes Utiles

```bash
# Tous les tests
pio test -e native

# Un test spécifique
pio test -e native -f test_nvs

# Mode verbose
pio test -e native -v

# Avec couverture (si configuré)
pio test -e native --coverage
```
