# Plan de Tests Unitaires - Projet FFP5CS ESP32

## 📋 Vue d'ensemble

Ce document décrit la stratégie complète de mise en place des tests unitaires pour le firmware ESP32 du projet FFP5CS.

### Objectifs
- ✅ Assurer la qualité et la fiabilité du code
- ✅ Faciliter la maintenance et le refactoring
- ✅ Détecter les régressions rapidement
- ✅ Documenter le comportement attendu des modules
- ✅ Réduire les bugs en production

## 🏗️ Architecture des Tests

### Environnement de test
- **Framework**: Unity Test Framework (intégré à PlatformIO)
- **Environnement**: Native (tests sur PC, pas sur hardware)
- **Language**: C++17

### Structure des dossiers
```
test/
├── unit/                    # Tests unitaires
│   ├── test_mocks.h         # Mocks Arduino/ESP32
│   ├── test_timer_manager.cpp
│   ├── test_rate_limiter.cpp
│   ├── test_config_manager.cpp
│   ├── test_data_queue.cpp
│   └── ...
├── integration/             # Tests d'intégration (futur)
└── hardware/                # Tests hardware spécifiques (existant)
    └── test_modem_sleep.cpp
```

## 📊 Modules Prioritaires pour Tests Unitaires

### Phase 1: Modules utilitaires (Sans dépendances hardware)

#### 1. **TimerManager** ✅ (Implémenté)
**Priorité**: Haute  
**Couverture cible**: 90%+

**Tests à implémenter**:
- ✅ Initialisation du manager
- ✅ Ajout/suppression de timers
- ✅ Déclenchement des callbacks
- ✅ Gestion enable/disable
- ✅ Mise à jour d'intervalle
- ✅ Gestion suspend/resume
- ✅ Statistiques d'exécution
- ✅ Gestion de la capacité maximale

**Dépendances mockées**:
- `millis()` / `micros()` → Mock time
- `Serial` → Mock output

---

#### 2. **RateLimiter** ✅ (Implémenté)
**Priorité**: Haute  
**Couverture cible**: 90%+

**Tests à implémenter**:
- ✅ Limitation basique de requêtes
- ✅ Fenêtres temporelles glissantes
- ✅ Séparation multi-clients
- ✅ Séparation multi-endpoints
- ✅ Endpoints sans limite
- ✅ Reset client
- ✅ Cleanup des entrées expirées
- ✅ Statistiques

**Dépendances mockées**:
- `millis()` → Mock time
- `String` Arduino → Utilise std::string ou mock

---

#### 3. **ConfigManager / NVSManager**
**Priorité**: Moyenne  
**Couverture cible**: 80%+

**Tests à implémenter**:
- Lecture/écriture de valeurs (int, float, string)
- Valeurs par défaut
- Validation de valeurs (min/max)
- Persistance entre sessions
- Gestion des erreurs (mémoire, clés invalides)
- Cleanup de clés obsolètes

**Dépendances à mocker**:
- `Preferences` (NVS) → Mock storage en mémoire

---

#### 4. **DataQueue**
**Priorité**: Moyenne  
**Couverture cible**: 85%+

**Tests à implémenter**:
- Ajout/retrait d'éléments (FIFO)
- Gestion de la capacité
- Queue vide/pleine
- Thread-safety (si applicable)
- Overwrite quand plein

---

#### 5. **DisplayTextUtils** (si existe)
**Priorité**: Faible  
**Couverture cible**: 75%+

**Tests à implémenter**:
- Formatage de textes
- Troncature selon largeur
- Conversion unités
- Padding/alignement

---

### Phase 2: Modules avec dépendances hardware légères

#### 6. **SensorCache**
**Priorité**: Moyenne  
**Couverture cible**: 75%+

**Tests à implémenter**:
- Mise en cache des lectures
- Expiration du cache
- Force update
- Invalidation
- Thread-safety (mutex)

**Dépendances à mocker**:
- `SystemSensors` → Mock sensor readings
- FreeRTOS (mutex) → Mock ou stub

---

#### 7. **Automatism Logic** (Modules découplés)
**Priorité**: Haute  
**Couverture cible**: 80%+

**Stratégie**: Tester la logique métier sans hardware

**Modules à tester**:
- `automatism_feeding.cpp` - Logique de nourrissage
- `automatism_refill.cpp` - Logique de remplissage
- `automatism_alert_controller.cpp` - Détection d'alertes

**Tests pour chaque module**:
- Calcul des conditions de déclenchement
- Gestion des seuils
- États et transitions
- Timeouts et retry

**Dépendances à mocker**:
- `SystemSensors` → Mock readings
- `SystemActuators` → Mock actuators
- `ConfigManager` → Mock config

---

### Phase 3: Modules avec dépendances hardware fortes

#### 8. **Sensors (UltrasonicManager, AirSensor, etc.)**
**Priorité**: Faible (préférer tests d'intégration)  
**Couverture cible**: 60%+

**Stratégie**: Tests limités à la logique de filtrage

**Tests possibles**:
- Filtrage de valeurs aberrantes
- Calcul de moyennes
- Validation plages (min/max)
- Historique glissant

**Dépendances à mocker**:
- `pulseIn()` / GPIO → Mock readings
- `DHT.read()` → Mock temp/humidity

---

#### 9. **Network Managers (WiFi, WebClient, WebServer)**
**Priorité**: Faible (préférer tests d'intégration)  
**Couverture cible**: 50%+

**Stratégie**: Tests limités à la logique de configuration

**Tests possibles**:
- Parsing de configurations WiFi
- Validation d'URLs
- Formatage de payloads JSON

---

## 🧪 Exemples de Tests

### Structure d'un test Unity

```cpp
#include <unity.h>
#include "test_mocks.h"
#include "module_to_test.h"

void setUp(void) {
  // Initialisation avant chaque test
  reset_mock_time();
}

void tearDown(void) {
  // Nettoyage après chaque test
}

void test_feature_basic_functionality() {
  // Arrange
  Module module;
  module.init();
  
  // Act
  bool result = module.doSomething();
  
  // Assert
  TEST_ASSERT_TRUE(result);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_feature_basic_functionality);
  return UNITY_END();
}
```

### Pattern AAA (Arrange-Act-Assert)

Chaque test doit suivre ce pattern :
1. **Arrange**: Configuration initiale (mocks, données de test)
2. **Act**: Exécution de la fonctionnalité testée
3. **Assert**: Vérification des résultats

## 🚀 Exécution des Tests

### Commandes PlatformIO

```bash
# Exécuter tous les tests unitaires
pio test -e native

# Exécuter un test spécifique
pio test -e native -f test_timer_manager

# Tests avec verbosité
pio test -e native -v

# Tests avec couverture de code (si configuré)
pio test -e native --coverage
```

### Script PowerShell

```powershell
# test_unit_all.ps1
Write-Host "🧪 Exécution des tests unitaires..." -ForegroundColor Cyan
pio test -e native -v
if ($LASTEXITCODE -eq 0) {
    Write-Host "✅ Tous les tests passent" -ForegroundColor Green
} else {
    Write-Host "❌ Certains tests ont échoué" -ForegroundColor Red
    exit 1
}
```

## 📈 Métriques de Qualité

### Objectifs de couverture

| Module | Couverture cible | Priorité |
|--------|------------------|----------|
| TimerManager | 90%+ | Haute |
| RateLimiter | 90%+ | Haute |
| ConfigManager | 80%+ | Moyenne |
| DataQueue | 85%+ | Moyenne |
| Automatism modules | 80%+ | Haute |
| SensorCache | 75%+ | Moyenne |
| Utils (display, etc.) | 75%+ | Faible |

### Critères de succès

- ✅ Tous les tests passent avant chaque commit
- ✅ Couverture ≥ objectif pour modules prioritaires
- ✅ Tests exécutés en < 5 secondes
- ✅ Aucune régression détectée

## 🔄 Intégration CI/CD (Futur)

### GitHub Actions / GitLab CI

```yaml
test-unit:
  runs-on: ubuntu-latest
  steps:
    - uses: actions/checkout@v3
    - uses: actions/setup-python@v4
    - run: pip install platformio
    - run: pio test -e native
```

## 📝 Bonnes Pratiques

### 1. Nommage des tests
- `test_module_feature_expected_behavior()`
- Exemple: `test_timer_manager_process_triggers_callback()`

### 2. Un test = une fonctionnalité
- Un test ne doit vérifier qu'une seule chose
- Utiliser plusieurs tests pour plusieurs aspects

### 3. Tests indépendants
- Chaque test doit pouvoir s'exécuter seul
- Pas de dépendance entre tests
- Utiliser `setUp()` et `tearDown()` pour isolation

### 4. Données de test claires
- Utiliser des valeurs explicites
- Éviter les valeurs magiques
- Documenter les cas limites

### 5. Mocks légers
- Mocker uniquement les dépendances externes
- Garder la logique métier réelle
- Utiliser des mocks simples (pas de frameworks complexes)

## 🐛 Gestion des Bugs

### Quand ajouter un test

1. **Avant de corriger un bug**:
   - Créer un test qui reproduit le bug
   - Vérifier qu'il échoue
   - Corriger le bug
   - Vérifier que le test passe

2. **Après détection d'un bug**:
   - Ajouter un test de régression
   - S'assurer que le bug ne reviendra pas

## 📚 Ressources

- [Unity Test Framework](https://github.com/ThrowTheSwitch/Unity)
- [PlatformIO Testing](https://docs.platformio.org/en/latest/advanced/unit-testing/index.html)
- [Test-Driven Development](https://fr.wikipedia.org/wiki/Test_driven_development)

## 🔄 Mise à jour du Plan

Ce plan doit être mis à jour régulièrement :
- Quand de nouveaux modules sont ajoutés
- Quand des bugs révèlent des lacunes dans les tests
- Quand la couverture atteint les objectifs

---

**Dernière mise à jour**: 2025-01-XX  
**Version**: 1.0
