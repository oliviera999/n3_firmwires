# Analyse Complète du Fonctionnement OLED - ESP32 FFP5CS

**Date d'analyse** : 11 janvier 2026  
**Version analysée** : v11.123 (code actuel)  
**Fichiers analysés** : 
- `src/display_view.cpp` / `include/display_view.h`
- `src/automatism/automatism_display_controller.cpp` / `.h`
- `src/system_boot.cpp`
- `src/app_tasks.cpp`
- `include/pins.h`
- `include/config.h`

---

## 📋 RÉSUMÉ EXÉCUTIF

### ✅ Points Fonctionnels
1. **Architecture complète** : Système d'affichage OLED bien structuré avec séparation des responsabilités
2. **Tâche dédiée** : `displayTask` s'exécute indépendamment avec cadence dynamique (80-250ms)
3. **Gestion robuste I2C** : Détection automatique avec mode dégradé si OLED absente
4. **Cache intelligent** : Optimisation des rafraîchissements pour réduire les communications I2C
5. **Affichage implémenté** : La méthode `updateDisplay()` est complète depuis v11.120

### ⚠️ Points d'Attention
1. **Détection I2C** : Peut échouer si câblage défectueux (erreur NACK)
2. **Splash screen** : Durées multiples (3s, 5s) selon le chemin d'exécution
3. **Intervalle dynamique** : Utilisé mais pourrait être mieux documenté

---

## 🏗️ ARCHITECTURE GÉNÉRALE

### Structure en Couches

```
┌─────────────────────────────────────────┐
│   displayTask (FreeRTOS)               │
│   - Priorité: 1 (BASSE)                │
│   - Stack: 4096 bytes                  │
│   - Core: 1                             │
│   - Intervalle: 80-250ms (dynamique)  │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│   Automatism::updateDisplay()           │
│   - Appelle le contrôleur d'affichage  │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│   AutomatismDisplayController           │
│   - Gère la logique d'affichage         │
│   - Basculement écran principal/vars    │
│   - Gestion des countdowns              │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│   DisplayView                           │
│   - API bas niveau (Adafruit_SSD1306)   │
│   - Gestion I2C et initialisation      │
│   - Cache et optimisations              │
└─────────────────────────────────────────┘
```

---

## 🔧 COMPOSANTS PRINCIPAUX

### 1. **DisplayView** (`src/display_view.cpp`)

#### Initialisation (`begin()`)
- **Lignes 214-331** : Initialisation complète de l'OLED
- **Détection I2C** (lignes 254-265) :
  ```cpp
  Wire.beginTransmission(Pins::OLED_ADDR);
  byte error = Wire.endTransmission();
  if (error != 0) {
    // Mode dégradé activé
    _present = false;
    return false;
  }
  ```
- **Splash screen** (lignes 280-315) :
  - Durée : **3 secondes** (`_splashUntil = millis() + 3000`)
  - Affiche : "Projet farmflow FFP5", version, logo N3
  - Non bloquant (géré par `splashActive()`)

#### Méthodes d'Affichage Principales

1. **`showMain()`** (lignes 361-430)
   - Affiche les données des capteurs (température, humidité, niveaux, luminosité, heure)
   - Utilise le cache pour éviter les rafraîchissements inutiles
   - Gère l'overlay OTA si actif

2. **`showVariables()`** (lignes 432-438)
   - Affiche l'état des actionneurs (pompes, chauffage, lumière)
   - Écran alternatif au principal

3. **`showCountdown()`** / **`showFeedingCountdown()`** (lignes 179-207)
   - Affichage des décomptes (nourrissage, phases)
   - Rafraîchissement immédiat (fluide)

4. **`drawStatus()`** / **`drawStatusEx()`** (lignes 115-177)
   - Barre d'état en haut de l'écran
   - Icônes : WiFi (RSSI), envoi/réception, mail (clignotement), marée
   - Cache intelligent pour éviter les redessins

#### Gestion du Cache (`include/display_cache.h`)
- **StatusCache** : Cache pour la barre d'état
- **MainCache** : Cache pour l'écran principal
- **Seuils de changement** :
  - Température : 0.1°C
  - Humidité : 0.5%
  - Niveaux : 1 cm
- **Impact** : Réduction significative des communications I2C

#### Verrouillage d'Écran (`lockScreen()` / `isLocked()`)
- **Lignes 344-359** : Protection contre les superpositions
- Auto-expiration avec gestion du overflow de `millis()`
- Utilisé pour les écrans OTA, sleep, diagnostics

---

### 2. **AutomatismDisplayController** (`src/automatism/automatism_display_controller.cpp`)

#### Méthode Principale : `updateDisplay()`
- **Lignes 41-197** : Logique complète d'affichage (✅ **CORRIGÉE v11.120**)

**Fonctionnement** :
1. **Vérifications préalables** (lignes 47-56) :
   - Présence OLED
   - Écran verrouillé
   - Gestion du splash screen (timeout 5s)

2. **Basculement automatique d'écran** (lignes 58-65) :
   - Intervalle : **4 secondes** (`SCREEN_SWITCH_INTERVAL_MS`)
   - Alterne entre écran principal et écran variables

3. **Intervalle de rafraîchissement dynamique** (lignes 67-81) :
   - **Countdown actif** : 250ms (fluide pour décomptes)
   - **Mode normal** : 80ms (`OLED_INTERVAL_MS`)
   - Détection automatique des phases de nourrissage

4. **Affichage selon l'état** (lignes 137-197) :
   - **Countdown actif** → `showCountdown()`
   - **Phase de nourrissage** → `showFeedingCountdown()`
   - **Mode normal** → `showMain()` ou `showVariables()` selon `_oledToggle`
   - **Barre d'état** toujours affichée

#### Points Clés
- ✅ **Implémentation complète** : Plus de TODO, tout est fonctionnel
- ✅ **Accès aux membres privés** : Via `friend class` dans `Automatism`
- ✅ **Calcul de la marée** : Utilise `_lastDiffMaree` depuis `Automatism`

---

### 3. **Tâche displayTask** (`src/app_tasks.cpp`)

#### Définition (lignes 89-113)
```cpp
void displayTask(void* pv) {
  // Enregistrement watchdog
  // Boucle infinie
  for (;;) {
    if (g_ctx->otaManager.isUpdating()) {
      vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(250));
      continue;
    }
    g_ctx->automatism.updateDisplay();
    uint32_t intervalMs = g_ctx->automatism.getRecommendedDisplayIntervalMs();
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(intervalMs));
  }
}
```

#### Caractéristiques
- **Priorité** : 1 (BASSE) - ne bloque pas les autres tâches
- **Stack** : 4096 bytes
- **Core** : 1 (même core que `automationTask`)
- **Intervalle dynamique** : 80-250ms selon l'état
- **Pause pendant OTA** : 250ms pour éviter les conflits

#### Création (lignes 339-345)
```cpp
xTaskCreatePinnedToCore(displayTask,
                        "displayTask",
                        TaskConfig::DISPLAY_TASK_STACK_SIZE,
                        nullptr,
                        TaskConfig::DISPLAY_TASK_PRIORITY,
                        &g_displayTaskHandle,
                        TaskConfig::DISPLAY_TASK_CORE_ID);
```

---

## 🔌 CONFIGURATION MATÉRIELLE

### Pins I2C (`include/pins.h`)

#### ESP32-WROOM (par défaut)
- **SDA** : GPIO 21
- **SCL** : GPIO 22
- **Adresse OLED** : 0x3C

#### ESP32-S3
- **SDA** : GPIO 8
- **SCL** : GPIO 9
- **Adresse OLED** : 0x3C

### Configuration (`include/config.h`)
- **Largeur** : 128 pixels
- **Hauteur** : 64 pixels
- **Adresse I2C** : 0x3C
- **Feature flag** : `FEATURE_OLED` (défini dans `platformio.ini`)

---

## 🔄 FLUX D'EXÉCUTION

### Séquence de Démarrage

1. **`system_boot.cpp::initializeDisplay()`** (ligne 189)
   - Appelle `ctx.display.begin()`
   - Logs de debug activés

2. **`DisplayView::begin()`**
   - Initialisation I2C
   - Détection OLED (test `Wire.beginTransmission()`)
   - Si détectée : initialisation `Adafruit_SSD1306`
   - Affichage splash screen (3 secondes)
   - Configuration CP437 pour accents

3. **`system_boot.cpp::finalizeDisplay()`** (ligne 251)
   - Force la fin du splash : `ctx.display.forceEndSplash()`
   - Affiche "Init ok"

4. **`app_tasks.cpp::start()`**
   - Création de `displayTask`
   - Démarrage de la boucle d'affichage

### Boucle d'Affichage Normale

```
displayTask (toutes les 80-250ms)
  └─> Automatism::updateDisplay()
      └─> AutomatismDisplayController::updateDisplay()
          ├─> Vérifications (présence, verrouillage, splash)
          ├─> Calcul intervalle dynamique
          ├─> Détection countdown/phase nourrissage
          └─> Affichage :
              ├─> showCountdown() / showFeedingCountdown() (si actif)
              ├─> showMain() / showVariables() (selon _oledToggle)
              └─> drawStatus() (barre d'état)
```

---

## ⚙️ OPTIMISATIONS IMPLÉMENTÉES

### 1. **Cache Intelligent**
- **StatusCache** : Évite les redessins de la barre d'état si valeurs inchangées
- **MainCache** : Évite les redessins de l'écran principal si valeurs similaires
- **Seuils** : Température (0.1°C), humidité (0.5%), niveaux (1 cm)

### 2. **Mode Update Différé**
- **`beginUpdate()` / `endUpdate()`** : Groupe les modifications
- **`_needsFlush`** : Flag pour flush conditionnel
- **`_immediateMode`** : Mode flush immédiat pour changements critiques

### 3. **Intervalle Dynamique**
- **80ms** : Mode normal (12.5 FPS)
- **250ms** : Mode countdown (4 FPS, mais fluide pour décomptes)
- **Adaptation automatique** selon l'état

### 4. **Réduction Communications I2C**
- **Suppression vérifications répétées** : Plus de `Wire.beginTransmission()` dans les boucles
- **Cache** : Évite les redessins inutiles
- **Flush conditionnel** : `display()` appelé seulement si nécessaire

---

## 🐛 PROBLÈMES IDENTIFIÉS ET CORRECTIONS

### ✅ CORRIGÉ : `AutomatismDisplayController::updateDisplay()` Incomplet
- **Version** : v11.120
- **Problème** : Méthode ne faisait aucun affichage réel
- **Solution** : Implémentation complète avec appels à `showMain()`, `showVariables()`, `showCountdown()`
- **État** : ✅ **RÉSOLU**

### ✅ CORRIGÉ : Erreurs I2C Massives
- **Version** : v11.57
- **Problème** : Vérifications I2C répétées dans les boucles d'affichage
- **Solution** : Vérification unique à l'initialisation, mode dégradé si erreur
- **État** : ✅ **RÉSOLU**

### ✅ CORRIGÉ : Syntaxe Erreur dans `begin()`
- **Version** : v11.57
- **Problème** : Accolade fermante manquante, code exécuté hors du `if`
- **Solution** : Correction de la structure `if-else`
- **État** : ✅ **RÉSOLU**

### ⚠️ À SURVEILLER : Détection I2C
- **Problème** : Peut échouer si câblage défectueux
- **Symptômes** : `[OLED] Non détectée sur I2C - Erreur: 1 ou 2`
- **Causes possibles** :
  1. Câblage I2C défectueux (SDA/SCL)
  2. OLED non alimentée (3.3V ou 5V)
  3. Pull-ups I2C manquants (4.7kΩ recommandés)
  4. Adresse I2C incorrecte (0x3C vs 0x3D)
  5. Timing I2C insuffisant
- **Recommandation** : Ajouter retry logic avec délais progressifs (3 tentatives)

### ⚠️ À AMÉLIORER : Gestion Splash Screen
- **Problème** : Durées multiples selon le chemin d'exécution
  - `DisplayView::begin()` : 3 secondes
  - `AutomatismDisplayController` : timeout 5 secondes
  - `system_boot.cpp::finalizeDisplay()` : force immédiate
- **Recommandation** : Unifier dans une constante `DisplayConfig::SPLASH_DURATION_MS`

---

## 📊 STATISTIQUES ET MÉTRIQUES

| Métrique | Valeur | État |
|----------|--------|------|
| **Fichiers principaux** | 5 fichiers | ✅ |
| **Lignes de code OLED** | ~800 lignes | ✅ |
| **Tâche displayTask** | Priorité 1, Stack 4096 | ✅ |
| **Intervalle normal** | 80ms (12.5 FPS) | ✅ |
| **Intervalle countdown** | 250ms (4 FPS) | ✅ |
| **Cache Status** | Implémenté | ✅ |
| **Cache Main** | Implémenté | ✅ |
| **Cache Variables** | ❌ Manquant | ⚠️ |
| **Détection I2C** | Robuste avec mode dégradé | ✅ |
| **Gestion erreurs** | Mode dégradé automatique | ✅ |

---

## 🎯 POINTS FORTS

1. ✅ **Architecture propre** : Séparation claire des responsabilités
2. ✅ **Tâche dédiée** : Affichage indépendant de la logique métier
3. ✅ **Cache intelligent** : Réduction significative des communications I2C
4. ✅ **Gestion robuste** : Mode dégradé si OLED absente
5. ✅ **Affichage complet** : Tous les écrans implémentés (main, variables, countdown, OTA, sleep)
6. ✅ **Optimisations** : Intervalle dynamique, flush conditionnel, cache
7. ✅ **Support UTF-8** : Conversion CP437 pour accents
8. ✅ **Verrouillage écran** : Protection contre superpositions

---

## 🔍 POINTS D'AMÉLIORATION

### Priorité 1 - Recommandations
1. **Ajouter retry logic I2C** : 3 tentatives avec délais progressifs lors de l'initialisation
2. **Unifier gestion splash** : Utiliser `DisplayConfig::SPLASH_DURATION_MS` partout
3. **Ajouter VariablesCache** : Cache pour l'écran variables (optimisation)

### Priorité 2 - Optimisations
4. **Timeout sur `_isDisplaying`** : Max 5 secondes pour éviter blocage permanent
5. **Documentation intervalle** : Mieux documenter la logique d'intervalle dynamique
6. **Logging conditionnel** : Réduire les logs en production

---

## 📝 RECOMMANDATIONS FINALES

### Immédiat
1. ✅ **Aucune action critique** : Le système OLED est fonctionnel et bien implémenté

### Court Terme
2. **Tester la détection I2C** : Vérifier avec scanner I2C si problèmes de détection
3. **Unifier splash screen** : Simplifier la gestion des durées

### Long Terme
4. **Ajouter VariablesCache** : Optimisation supplémentaire
5. **Améliorer retry logic** : Robustesse I2C accrue

---

## ✅ CONCLUSION

Le système OLED est **bien implémenté et fonctionnel**. L'architecture est propre, les optimisations sont en place, et la gestion d'erreurs est robuste. Les corrections apportées en v11.120 ont résolu les problèmes critiques identifiés précédemment.

**État global** : ✅ **FONCTIONNEL ET OPTIMISÉ**

---

*Analyse générée le 11 janvier 2026 à partir du code v11.123*
