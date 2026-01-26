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

- `test_mocks.h` - Mocks pour fonctions Arduino/ESP32
- `test_timer_manager.cpp` - Tests pour TimerManager
- `test_rate_limiter.cpp` - Tests pour RateLimiter
- `README.md` - Ce fichier

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
pio test -e native -f test_timer_manager

# Mode verbose
pio test -e native -v

# Avec couverture (si configuré)
pio test -e native --coverage
```
