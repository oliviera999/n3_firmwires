# 🚀 Guide de Migration : `config.h` → `project_config.h`

> ℹ️ **Note (nov. 2025)**  
> Les anciens namespaces de compatibilité (`CompatibilityAliases`, `CompatibilityUtils`, `Config`) ont été supprimés. Ce guide fait désormais référence uniquement aux namespaces natifs (`ProjectConfig`, `ApiConfig`, `EmailConfig`, etc.).

## 📋 **Vue d'ensemble**

Ce guide explique comment migrer votre code de l'ancien système de configuration (`config.h`) vers le nouveau système unifié (`project_config.h`).

---

## ✅ **Important : `config.h` supprimé**

**`config.h` a été complètement supprimé !**

### **Migration terminée :**
```
✅ config.h supprimé avec succès
✅ Tous les fichiers utilisent maintenant project_config.h
✅ Architecture unifiée et optimisée
```

---

## 🔄 **Migration des includes**

### **✅ Migration terminée :**
```cpp
// config.h a été supprimé - utilisez directement :
#include "project_config.h"
```

### **Fichiers déjà migrés :**
- ✅ `src/automatism.cpp`
- ✅ `src/app.cpp`
- ✅ `src/wifi_manager.cpp`
- ✅ `src/display_view.cpp`
- ✅ `src/web_server.cpp`
- ✅ `src/ota_manager.cpp`
- ✅ `src/mailer.cpp`
- ✅ `src/config.cpp`
- ✅ `include/automatism.h`
- ✅ `include/display_view.h`
- ✅ `include/mailer.h`
- ✅ `include/web_server.h`

---

## 🏗️ **Architecture du nouveau système**

### **Structure de `project_config.h` :**

```cpp
// =============================================================================
// CONFIGURATION CENTRALISÉE
// =============================================================================

namespace ProjectConfig {
    // Configuration générale
    constexpr const char* VERSION = "10.0.0";
    constexpr const char* BOARD_TYPE = "ESP32-S3";
    
    // Configuration des profils
    namespace Profiles {
        constexpr bool PROD = true;
        constexpr bool DEV = false;
    }
    
    // Configuration des fonctionnalités
    namespace Features {
        constexpr bool OLED_ENABLED = true;
        constexpr bool OTA_ENABLED = true;
        constexpr bool MAIL_ENABLED = true;
        // ... autres fonctionnalités
    }
}

// =============================================================================
// ALIAS DE COMPATIBILITÉ (temporaire)
// =============================================================================
> *Section historique (legacy)* : l'extrait ci-dessous documente l'ancien namespace supprimé en v11.118.

namespace CompatibilityAliases {
    // Anciennes constantes redirigées
    constexpr bool FEATURE_OLED = Features::OLED_ENABLED;
    constexpr bool FEATURE_OTA = Features::OTA_ENABLED;
    constexpr bool FEATURE_MAIL = Features::MAIL_ENABLED;
    // ... autres alias
}
```

---

## 🔧 **Migration des constantes**

### **Ancien système (déprécié) :**
```cpp
if (FEATURE_OLED) {
    // Code OLED
}
```

### **Nouveau système (recommandé) :**
```cpp
if (ProjectConfig::Features::OLED_ENABLED) {
    // Code OLED
}
```

### **Système de compatibilité (temporaire) :**
```cpp
if (CompatibilityAliases::FEATURE_OLED) {
    // Code OLED (fonctionne encore)
}
```

---

## 🛠️ **Migration des fonctions utilitaires**

### **Ancien système :**
```cpp
String env = getEnvironmentName();
bool debug = isDebugMode();
```

### **Nouveau système :**
```cpp
String env = CompatibilityUtils::getEnvironmentName();
bool debug = CompatibilityUtils::isDebugMode();
```

---

## 🐛 **Migration des macros de debug**

### **Ancien système :**
```cpp
DEBUG_PRINT("Message de debug");
DEBUG_PRINTF("Format: %d", value);
VERBOSE_PRINT("Message verbeux");
```

### **Nouveau système :**
```cpp
LOG_DEBUG("Message de debug");
LOG_DEBUG("Format: %d", value);
LOG_VERBOSE("Message verbeux");
```

### **Système de compatibilité (temporaire) :**
```cpp
// Les anciennes macros fonctionnent encore
DEBUG_PRINT("Message de debug");  // Redirigé vers LOG_DEBUG
```

---

## 📝 **Exemples de migration**

### **Exemple 1 : Fichier simple**

**Avant :**
```cpp
#include "config.h"

void setup() {
    if (FEATURE_OLED) {
        Serial.println("OLED activé");
    }
    
    DEBUG_PRINT("Démarrage");
}
```

**Après :**
```cpp
#include "project_config.h"

void setup() {
    if (ProjectConfig::Features::OLED_ENABLED) {
        Serial.println("OLED activé");
    }
    
    LOG_DEBUG("Démarrage");
}
```

### **Exemple 2 : Classe avec configuration**

**Avant :**
```cpp
#include "config.h"

class MyClass {
public:
    void init() {
        if (FEATURE_MAIL) {
            // Initialiser le mail
        }
    }
};
```

**Après :**
```cpp
#include "project_config.h"

class MyClass {
public:
    void init() {
        if (ProjectConfig::Features::MAIL_ENABLED) {
            // Initialiser le mail
        }
    }
};
```

---

## ⚡ **Migration progressive recommandée**

### **Phase 1 : Migration des includes (FAIT)**
- ✅ Remplacer `#include "config.h"` par `#include "project_config.h"`
- ✅ Tester la compilation

### **Phase 2 : Migration des constantes (EN COURS)**
- 🔄 Remplacer `FEATURE_*` par `ProjectConfig::Features::*`
- 🔄 Remplacer `ENVIRONMENT_*` par `ProjectConfig::Profiles::*`
- 🔄 Tester les fonctionnalités

### **Phase 3 : Migration des fonctions (FUTUR)**
- ⏳ Remplacer `getEnvironmentName()` par `CompatibilityUtils::getEnvironmentName()`
- ⏳ Remplacer les macros de debug par `LOG_*`
- ⏳ Tester et valider

### **Phase 4 : Nettoyage (v11.0)**
- ⏳ Supprimer `config.h`
- ⏳ Supprimer les alias de compatibilité
- ⏳ Finaliser l'architecture

---

## 🧪 **Tests de validation**

### **Checklist de migration :**

- [ ] **Compilation** : Le code compile sans erreurs
- [ ] **Fonctionnalités** : Toutes les fonctionnalités marchent
- [ ] **Performance** : Pas de régression de performance
- [ ] **Mémoire** : Utilisation mémoire optimale
- [ ] **Tests** : Tests unitaires passent

### **Commandes de test :**
```bash
# Test de compilation
pio run --target checkprogsize

# Test de taille mémoire
pio run --target checkprogsize --verbose

# Test de fonctionnalités
pio run --target upload
```

---

## 🆘 **Dépannage**

### **Problèmes courants :**

#### **1. Erreur de compilation : "FEATURE_OLED not declared"**
**Solution (legacy)** : Utiliser `CompatibilityAliases::FEATURE_OLED` ou désormais `ProjectConfig::Features::OLED_ENABLED`

#### **2. Conflit de macros DEBUG_PRINT**
**Solution :** Les macros sont maintenant définies dans `config.h` pour éviter les conflits

#### **3. Erreur AsyncWebServer**
**Solution :** Utiliser la forward declaration dans `web_server.h`

### **Support :**
- 📧 **Email** : [votre-email@domain.com]
- 📖 **Documentation** : Voir `CONFIG_MIGRATION_SUMMARY.md`
- 🐛 **Issues** : Créer une issue GitHub

---

## 📚 **Ressources**

### **Fichiers de référence :**
- `include/project_config.h` : Configuration principale
- `include/config.h` : Redirecteur de compatibilité
- `CONFIG_MIGRATION_SUMMARY.md` : Résumé de la migration

### **Exemples de code :**
- Voir les fichiers déjà migrés dans `src/`
- Voir les headers migrés dans `include/`

---

## 🎯 **Prochaines étapes**

1. **Immédiat** : Migrer les constantes restantes
2. **Court terme** : Tester toutes les fonctionnalités
3. **Moyen terme** : Migrer les fonctions utilitaires
4. **Long terme** : Supprimer `config.h` en v11.0

---

*Guide créé le $(date) - Version 10.0.0*
