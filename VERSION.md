# VERSION.md - GPIO Parsing Unifié v11.68

## Version 11.68 - GPIO Parsing Unifié - Système Simplifié

**Date**: 2025-01-16  
**Type**: Refactoring majeur - Simplification architecture GPIO

### 🎯 Objectif
Remplacer le système complexe de parsing GPIO par une architecture simple et robuste basée sur une source unique de vérité.

### ✨ Nouvelles Fonctionnalités

#### 1. Table de Mapping Centralisée
- **Fichier**: `include/gpio_mapping.h`
- **Responsabilité**: Source unique de vérité pour tous les GPIO
- **Couverture**: GPIO physiques (0-39) + virtuels (100-116)
- **Types**: Actuator, Config (Int/Float/String/Bool)

#### 2. Parsing Unifié
- **Fichier**: `src/gpio_parser.cpp` + `include/gpio_parser.h`
- **Responsabilité**: Parse JSON serveur + applique changements + sauvegarde NVS
- **Simplification**: 6 étapes → 1 appel `GPIOParser::parseAndApply()`

#### 3. Notifications Instantanées
- **Fichier**: `src/gpio_notifier.cpp` + `include/gpio_notifier.h`
- **Responsabilité**: POST instantané des changements locaux
- **Format**: Payload partiel avec GPIO numérique uniquement

#### 4. Serveur Simplifié
- **Fichier**: `ffp3/src/Controller/OutputController.php`
- **Changement**: Format GPIO numérique uniquement (plus de doublons)
- **Compatibilité**: Interface web non impactée (lit BDD directement)

### 🔧 Modifications Techniques

#### ESP32 - Fichiers Modifiés
- `src/automatism.cpp` - **Simplifié** (6 étapes → 1 appel)
- `include/project_config.h` - Version incrémentée à 11.68

#### ESP32 - Fichiers Supprimés
- Code GPIO dynamiques (250 lignes)
- Appels aux fonctions obsolètes (380 lignes)

#### Serveur - Fichiers Modifiés  
- `ffp3/src/Controller/OutputController.php` - Format simplifié

### 📊 Métriques

#### Code Supprimé (~630 lignes)
- `normalizeServerKeys()` - 80 lignes
- `parseRemoteConfig()` - 50 lignes
- `handleRemoteFeedingCommands()` - 70 lignes
- `handleRemoteActuators()` - 180 lignes
- GPIO dynamiques boucle - 250 lignes

#### Code Ajouté (~350 lignes)
- `gpio_mapping.h` - 120 lignes
- `gpio_parser.cpp/h` - 150 lignes
- `gpio_notifier.cpp/h` - 80 lignes

#### Gain Net
- **-280 lignes** de code
- **Architecture simplifiée** (1 parser au lieu de 6 étapes)
- **Maintenabilité améliorée** (source unique de vérité)

### 🚀 Avantages

#### Robustesse
- ✅ Source unique de vérité (pas de désynchronisation)
- ✅ Sauvegarde NVS automatique pour tous les GPIO
- ✅ Parsing simple et direct

#### Performance  
- ✅ POST instantané des changements locaux
- ✅ Priorité locale simplifiée (fenêtre fixe 1 seconde)
- ✅ Pas de normalisation redondante

#### Maintenabilité
- ✅ Table centralisée pour ajout GPIO
- ✅ Types explicites avec validation
- ✅ Code réduit et plus lisible

### 🧪 Tests et Validation

#### Tests Automatisés
- `test_gpio_sync.py` - Test complet du système
- Vérification format serveur (GPIO numérique uniquement)
- Test POST partiel et synchronisation bidirectionnelle
- Validation mapping GPIO connu

#### Monitoring Obligatoire
- `monitor_90s_v11.68.ps1` - Monitoring spécialisé v11.68
- Analyse logs : GPIO, NVS, Parsing, Erreurs
- Focus sur stabilité système

### 📚 Documentation

#### Nouvelle Documentation
- `docs/technical/GPIO_ARCHITECTURE.md` - Architecture complète
- Table de mapping détaillée
- Flux de données et exemples
- Guide de migration

### ⚠️ Points d'Attention

#### Migration
- **Interface web**: Aucun impact (lit BDD directement)
- **Serveur**: Format simplifié mais compatible
- **ESP32**: Parsing unifié remplace 6 étapes

#### Monitoring Post-Déploiement
- **OBLIGATOIRE**: Monitoring 90 secondes après déploiement
- **Focus**: Stabilité système, GPIO, NVS, Parsing
- **Analyse**: Erreurs critiques, warnings, performance

### 🔄 Prochaines Étapes

#### Phase 2 (Optionnelle)
- Intégration GPIONotifier dans tous les actionneurs
- Tests unitaires GPIO parser et notifier
- Optimisations performance supplémentaires

#### Phase 3 (Future)
- Extension mapping pour nouveaux GPIO
- Interface de configuration dynamique
- Monitoring avancé des performances

### 📝 Notes de Développement

#### Architecture
Le nouveau système repose sur 3 piliers :
1. **Mapping centralisé** - Source unique de vérité
2. **Parsing unifié** - Logique simplifiée
3. **Notifications instantanées** - Synchronisation bidirectionnelle

#### Compatibilité
- **Rétrocompatible** avec interface web existante
- **Migration transparente** pour l'utilisateur final
- **Performance améliorée** sans impact fonctionnel

---

**Version précédente**: v11.67  
**Prochaine version**: v11.69 (corrections mineures si nécessaire)
