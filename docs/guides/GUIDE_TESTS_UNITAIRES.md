# Guide d'Utilisation des Tests Unitaires - FFP5CS

## 🎯 Introduction

Ce guide explique comment utiliser, créer et maintenir les tests unitaires pour le projet FFP5CS.

## 🚀 Démarrage Rapide

### 1. Installation des dépendances

Les dépendances sont déjà configurées dans `platformio.ini`. Assurez-vous d'avoir PlatformIO installé :

```bash
# Vérifier l'installation
pio --version
```

### 2. Exécuter les tests

```bash
# Tous les tests
pio test -e native

# Avec verbosité
pio test -e native -v

# Un test spécifique
pio test -e native -f test_timer_manager
```

### 3. Script PowerShell

```powershell
# Exécuter le script d'automatisation
.\scripts\test_unit_all.ps1
```

## 📝 Créer un Nouveau Test

### Étape 1: Créer le fichier de test

Créez un nouveau fichier dans `test/unit/` :

```cpp
// test/unit/test_mon_module.cpp
#include <unity.h>
#include "test_mocks.h"
#include "mon_module.h"

void setUp(void) {
  // Initialisation avant chaque test
  reset_mock_time();
}

void tearDown(void) {
  // Nettoyage après chaque test
}

void test_mon_module_fonction_basique() {
  // Arrange
  MonModule module;
  module.init();
  
  // Act
  int result = module.calculer(2, 3);
  
  // Assert
  TEST_ASSERT_EQUAL(5, result);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_mon_module_fonction_basique);
  return UNITY_END();
}
```

### Étape 2: Compiler et tester

```bash
pio test -e native -f test_mon_module
```

### Étape 3: Ajouter au script global

Le test sera automatiquement détecté par PlatformIO. Pas besoin de modification supplémentaire.

## 🧪 Assertions Unity Disponibles

### Assertions de base

```cpp
TEST_ASSERT_TRUE(condition);          // Vrai
TEST_ASSERT_FALSE(condition);         // Faux
TEST_ASSERT_NULL(pointer);            // Pointeur NULL
TEST_ASSERT_NOT_NULL(pointer);        // Pointeur non-NULL
```

### Assertions de comparaison

```cpp
TEST_ASSERT_EQUAL(expected, actual);           // Égalité
TEST_ASSERT_NOT_EQUAL(expected, actual);       // Non-égalité
TEST_ASSERT_GREATER_THAN(threshold, actual);   // Supérieur
TEST_ASSERT_LESS_THAN(threshold, actual);      // Inférieur
TEST_ASSERT_GREATER_OR_EQUAL(threshold, actual);
TEST_ASSERT_LESS_OR_EQUAL(threshold, actual);
```

### Assertions de chaînes

```cpp
TEST_ASSERT_EQUAL_STRING(expected, actual);    // C-strings
TEST_ASSERT_NOT_EQUAL_STRING(expected, actual);
```

### Assertions de flottants (avec tolérance)

```cpp
TEST_ASSERT_FLOAT_WITHIN(delta, expected, actual);
TEST_ASSERT_EQUAL_FLOAT(expected, actual);  // Tolérance par défaut
```

## 🔧 Mocks et Stubs

### Mock du temps (millis/micros)

```cpp
#include "test_mocks.h"

void test_avec_temps() {
  // Initialiser le temps à 1000ms
  setup_mock_time(1000, 0);
  
  // Avancer le temps de 500ms
  advance_time(500);
  
  // millis() retournera maintenant 1500
  unsigned long now = millis();
  TEST_ASSERT_EQUAL(1500, now);
}
```

### Mock de Serial

Serial est automatiquement mocké dans `test_mocks.h`. Les appels à `Serial.print()`, `Serial.println()`, etc. sont no-op en test.

### Créer un mock personnalisé

```cpp
// Dans votre fichier de test
class MockSensor {
private:
  float mock_temp = 25.0;
  
public:
  void setTemperature(float temp) {
    mock_temp = temp;
  }
  
  float readTemperature() {
    return mock_temp;
  }
};

void test_avec_mock_sensor() {
  MockSensor sensor;
  sensor.setTemperature(30.0);
  
  float temp = sensor.readTemperature();
  TEST_ASSERT_EQUAL_FLOAT(30.0, temp);
}
```

## 📋 Structure d'un Test Complet

### Exemple : Test d'un Manager avec état

```cpp
#include <unity.h>
#include "test_mocks.h"
#include "mon_manager.h"

// Variables globales pour les callbacks
static int callback_count = 0;
static int last_value = 0;

void my_callback(int value) {
  callback_count++;
  last_value = value;
}

void setUp(void) {
  reset_mock_time();
  callback_count = 0;
  last_value = 0;
}

void tearDown(void) {
  // Nettoyage si nécessaire
}

void test_manager_initialisation() {
  MonManager manager;
  manager.init();
  
  TEST_ASSERT_FALSE(manager.isRunning());
  TEST_ASSERT_EQUAL(0, manager.getCount());
}

void test_manager_add_item() {
  MonManager manager;
  manager.init();
  
  bool added = manager.addItem("test");
  TEST_ASSERT_TRUE(added);
  TEST_ASSERT_EQUAL(1, manager.getCount());
}

void test_manager_callback_invocation() {
  MonManager manager;
  manager.init();
  manager.setCallback(my_callback);
  
  manager.trigger(42);
  
  TEST_ASSERT_EQUAL(1, callback_count);
  TEST_ASSERT_EQUAL(42, last_value);
}

void test_manager_time_based_behavior() {
  MonManager manager;
  manager.init();
  setup_mock_time(1000, 0);
  
  manager.start(100); // Intervalle 100ms
  
  // Pas encore déclenché
  TEST_ASSERT_EQUAL(0, callback_count);
  
  // Après 110ms
  advance_time(110);
  manager.update();
  TEST_ASSERT_EQUAL(1, callback_count);
}

int main(void) {
  UNITY_BEGIN();
  
  RUN_TEST(test_manager_initialisation);
  RUN_TEST(test_manager_add_item);
  RUN_TEST(test_manager_callback_invocation);
  RUN_TEST(test_manager_time_based_behavior);
  
  return UNITY_END();
}
```

## 🐛 Déboguer un Test qui Échoue

### 1. Mode verbose

```bash
pio test -e native -v
```

### 2. Exécuter un seul test

```bash
pio test -e native -f test_mon_module
```

### 3. Ajouter des logs de debug

```cpp
void test_debug() {
  // Utiliser printf pour le debug (visible avec -v)
  printf("Debug: valeur = %d\n", valeur);
  
  TEST_ASSERT_EQUAL(expected, valeur);
}
```

### 4. Utiliser un debugger

```bash
# Compiler avec debug
pio test -e native --verbose

# Attacher un debugger (gdb/lldb)
gdb .pio/build/native/program
```

## 📊 Mesurer la Couverture de Code

### Configuration (optionnelle)

Ajoutez dans `platformio.ini` :

```ini
[env:native]
test_flags = 
    --coverage
```

### Exécuter avec couverture

```bash
pio test -e native --coverage
```

Les rapports de couverture seront générés dans `.pio/build/native/coverage/`.

## ✅ Checklist pour un Nouveau Test

- [ ] Fichier créé dans `test/unit/`
- [ ] Inclut `test_mocks.h` si nécessaire
- [ ] Fonction `setUp()` pour initialisation
- [ ] Fonction `tearDown()` pour nettoyage (si nécessaire)
- [ ] Tests nommés clairement: `test_module_feature_behavior`
- [ ] Utilise le pattern AAA (Arrange-Act-Assert)
- [ ] Tests indépendants (pas de dépendances entre tests)
- [ ] Couvre les cas normaux ET limites
- [ ] Compile sans warnings
- [ ] Tous les tests passent

## 🔄 Workflow Recommandé

### Avant de commiter

1. Exécuter tous les tests :
   ```bash
   pio test -e native
   ```

2. Vérifier qu'ils passent tous

3. Si nouveau code, ajouter des tests

### TDD (Test-Driven Development)

1. **Red**: Écrire un test qui échoue
2. **Green**: Écrire le code minimal pour passer le test
3. **Refactor**: Améliorer le code tout en gardant les tests verts

### Après un bug détecté

1. Créer un test qui reproduit le bug
2. Vérifier qu'il échoue
3. Corriger le bug
4. Vérifier que le test passe

## 📚 Ressources

- **Unity Documentation**: https://github.com/ThrowTheSwitch/Unity
- **PlatformIO Testing**: https://docs.platformio.org/en/latest/advanced/unit-testing/
- **Plan des tests**: Voir `docs/guides/PLAN_TESTS_UNITAIRES.md`

## 🆘 Problèmes Courants

### Erreur: "undefined reference to millis()"

**Solution**: Inclure `test_mocks.h` dans votre fichier de test.

### Erreur: "Serial is not defined"

**Solution**: Inclure `test_mocks.h` qui définit `Arduino::Serial`.

### Tests passent mais comportement incorrect sur hardware

**Cause**: Le mock ne reflète pas fidèlement le comportement réel.

**Solution**: Vérifier que le mock est correct, ou considérer un test d'intégration.

### Compilation échoue avec des erreurs Arduino

**Solution**: Vérifier que les flags `-DUNIT_TEST` et `-DARDUINO=0` sont définis dans `platformio.ini` pour l'environnement `native`.

---

**Dernière mise à jour**: 2025-01-XX  
**Version**: 1.0
