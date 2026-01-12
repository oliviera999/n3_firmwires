# Diagnostic OLED - ESP32 FFP5CS
**Date**: 10 janvier 2026  
**Version analysée**: v11.119  
**Fichiers analysés**: `display_view.cpp`, `automatism_display_controller.cpp`, `system_boot.cpp`

---

## 🔴 PROBLÈMES CRITIQUES DÉTECTÉS

### 1. **AutomatismDisplayController::updateDisplay() INCOMPLET**
**Fichier**: `src/automatism/automatism_display_controller.cpp:43-113`

**Problème**: La méthode `updateDisplay()` ne fait **aucun affichage réel**. Elle se contente de valider les valeurs mais n'appelle jamais `_display.showMain()`, `_display.showVariables()`, ou `_display.showCountdown()`.

**Code actuel (lignes 104-113)**:
```cpp
// Logique d'affichage complexe déléguée à Automatism::updateDisplayInternal pour l'instant
// car elle dépend de trop de variables privées de Automatism (countdown, phases, etc.)
// En attendant la migration complète en Phase 2.8, on appelle la méthode interne de core.
// Mais on prépare la structure ici.

// core.updateDisplayInternal(ctx); // Appel temporaire à l'ancienne logique

// TODO: Migrer ici toute la logique de showMain/showServerVars/showCountdown
// Cela nécessite d'exposer beaucoup de getters sur Automatism
```

**Impact**: 
- 🔴 **CRITIQUE** - L'OLED ne s'affiche **PAS** pendant le fonctionnement normal
- L'écran reste bloqué sur le splash screen ou l'écran de diagnostic initial
- Les utilisateurs ne voient aucune information en temps réel

**Preuve**: Dans `src/automatism.cpp:81-88`, `Automatism::updateDisplay()` appelle uniquement `_displayController.updateDisplay()`, qui ne fait rien.

**Recommandation URGENTE**:
1. ⚠️ **Implémenter immédiatement** la logique d'affichage dans `AutomatismDisplayController::updateDisplay()`
2. Appeler `_display.showMain()` avec les valeurs des capteurs
3. Gérer l'affichage des variables (`showVariables`) en fonction de `_oledToggle`
4. Gérer les countdowns de nourrissage si nécessaire

---

### 2. **OLED Non Détectée à l'Initialisation**
**Fichier**: `src/display_view.cpp:254-265`  
**Rapport**: `BILAN_MONITORING_v11.119_2026-01-10.md:102-119`

**Problème**: D'après le monitoring récent, l'OLED n'est **pas détectée** sur le bus I2C lors de l'initialisation.

**Erreurs détectées**:
```
E (1192) i2c.master: I2C hardware NACK detected
E (1193) i2c.master: I2C transaction unexpected nack detected
[OLED] Non détectée sur I2C (SDA:21, SCL:22, ADDR:0x3C) - Erreur: 1
[OLED] Mode dégradé activé (pas d'affichage)
```

**Impact**: 
- 🔴 **CRITIQUE si matériel connecté** - L'OLED est désactivée même si elle est présente
- 🟢 **Normal si matériel absent** - Le système fonctionne en mode dégradé

**Causes possibles**:
1. **Câblage I2C défectueux** (SDA=21, SCL=22)
2. **OLED non alimentée** (3.3V ou 5V selon modèle)
3. **Pull-ups I2C manquants ou incorrects** (4.7kΩ recommandés)
4. **Adresse I2C incorrecte** (0x3C vs 0x3D selon modèle)
5. **Problème de timing I2C** - délai de stabilisation insuffisant

**Code actuel (lignes 254-265)**:
```cpp
Wire.beginTransmission(Pins::OLED_ADDR);
byte error = Wire.endTransmission();

// Si erreur I2C, désactiver immédiatement pour éviter le spam
if (error != 0) {
  Serial.printf("[OLED] Non détectée sur I2C (SDA:%d, SCL:%d, ADDR:0x%02X) - Erreur: %d\n", 
                Pins::I2C_SDA, Pins::I2C_SCL, Pins::OLED_ADDR, error);
  Serial.println("[OLED] Mode dégradé activé (pas d'affichage)");
  _present = false;
  return false;
}
```

**Recommandation**:
1. Vérifier le câblage physique I2C
2. Vérifier l'alimentation de l'OLED (3.3V vs 5V)
3. Vérifier les pull-ups I2C (4.7kΩ sur SDA et SCL vers VCC)
4. Tester avec un scanner I2C pour détecter l'adresse réelle
5. Ajouter une retry logic avec délais progressifs (3 tentatives)

---

## 🟡 PROBLÈMES MOYENS DÉTECTÉS

### 3. **Gestion du Splash Screen Incohérente**
**Fichiers**: 
- `src/display_view.cpp:280-315` (initialisation splash)
- `src/automatism/automatism_display_controller.cpp:52-58` (gestion splash)
- `src/system_boot.cpp:234-243` (finalisation)

**Problème**: La gestion du splash screen est **dispersée** entre plusieurs fichiers et utilise des durées différentes :

1. **Durée initiale** (ligne 314): `_splashUntil = millis() + 3000;` → **3 secondes**
2. **Forçage dans system_boot** (ligne 239): `ctx.display.forceEndSplash();` → **Immédiat**
3. **Timeout dans display_controller** (ligne 55): `currentMillis - _splashStartTime > 5000` → **5 secondes**

**Impact**: 
- 🟡 **MOYEN** - Comportement imprévisible du splash screen
- Le splash peut s'afficher plus ou moins longtemps selon le chemin d'exécution
- Confusion pour l'utilisateur

**Recommandation**:
1. Unifier la durée du splash dans une constante : `DisplayConfig::SPLASH_DURATION_MS`
2. Utiliser uniquement `splashActive()` dans `DisplayView` pour vérifier l'état
3. Supprimer la logique redondante dans `AutomatismDisplayController`

---

### 4. **Intervalle de Rafraîchissement Non Utilisé**
**Fichier**: `src/automatism/automatism_display_controller.cpp:34-41, 74-78`

**Problème**: La méthode `getRecommendedDisplayIntervalMs()` retourne une valeur fixe (1000ms) mais n'est **jamais appelée**. L'intervalle est hardcodé dans `updateDisplay()`.

**Code actuel**:
```cpp
uint32_t AutomatismDisplayController::getRecommendedDisplayIntervalMs(uint32_t nowMs) const {
    // Si countdown actif, rafraîchissement rapide (250ms), sinon normal (1000ms)
    // Note: La détection du countdown se fait via le contexte ou l'état interne
    // Pour l'instant on garde une valeur par défaut safe
    return 1000u; 
    
    // TODO: Passer l'état isCountdownMode en paramètre pour plus de précision
}
```

**Dans `updateDisplay()` (ligne 74)**:
```cpp
const uint32_t displayInterval = isCountdownMode ? 250u : 1000u; // Hardcodé !
```

**Impact**: 
- 🟡 **MOYEN** - Code mort / non utilisé
- Logique dupliquée
- Pas d'optimisation dynamique selon l'état

**Recommandation**:
1. Utiliser `getRecommendedDisplayIntervalMs()` dans `updateDisplay()`
2. Passer l'état `isCountdownMode` comme paramètre si nécessaire
3. Supprimer le code dupliqué

---

### 5. **Cache Display Incomplet pour Variables**
**Fichier**: `include/display_cache.h`

**Problème**: Le `DisplayCache` ne gère que **deux types de cache** :
- `StatusCache` (barre d'état)
- `MainCache` (écran principal)

**MAIS** il manque un cache pour `showVariables()` (écran des variables système). Cela peut causer des rafraîchissements inutiles.

**Impact**: 
- 🟢 **FAIBLE** - Performance légèrement impactée lors du basculement d'écran
- Pas de problème fonctionnel, mais optimisation possible

**Recommandation**:
1. Ajouter un `VariablesCache` dans `DisplayCache`
2. Utiliser ce cache dans `showVariables()` pour éviter les rafraîchissements inutiles

---

### 6. **Vérification `_isDisplaying` Potentiellement Problématique**
**Fichier**: `src/display_view.cpp:380-385, 509, 553, 611`

**Problème**: Plusieurs méthodes vérifient `_isDisplaying` pour éviter les appels simultanés, mais cette logique peut **bloquer légitimement** des mises à jour si le flag n'est pas réinitialisé correctement.

**Exemples**:
- `showMain()` (ligne 381): `if (_isDisplaying && !_updateMode) return;`
- `showOtaProgress()` (ligne 509): `if(!_present || splashActive() || _isDisplaying) return;`
- `showOtaProgressEx()` (ligne 553): `if(!_present || splashActive() || _isDisplaying || isLocked()) return;`

**Impact**: 
- 🟡 **MOYEN** - Risque de blocage si `_isDisplaying` reste à `true` après une erreur
- Les écrans OTA ou de sleep peuvent être bloqués si un affichage précédent a échoué

**Recommandation**:
1. Ajouter un timeout sur `_isDisplaying` (max 5 secondes)
2. Réinitialiser `_isDisplaying` dans un destructeur RAII ou dans `endUpdate()`
3. Logger les cas où `_isDisplaying` bloque un affichage

---

## 🟢 POINTS POSITIFS

### 7. **Gestion Robuste des Erreurs I2C**
**Fichier**: `src/display_view.cpp:254-265`

**✅ Points positifs**:
- Détection I2C avant initialisation complète
- Mode dégradé automatique si OLED non détectée
- Messages d'erreur clairs et détaillés
- Pas de spam d'erreurs I2C répétées (vérification unique à l'init)

---

### 8. **Système de Cache Optimisé**
**Fichier**: `include/display_cache.h`, `src/display_view.cpp:124-132, 365-378`

**✅ Points positifs**:
- Cache intelligent pour éviter les rafraîchissements inutiles
- Comparaison des valeurs avec seuils (température: 0.1°C, humidité: 0.5%)
- Reset du cache lors des changements d'écran
- Réduction significative des communications I2C

---

### 9. **Gestion du Verrouillage d'Écran (Screen Lock)**
**Fichier**: `src/display_view.cpp:344-359`

**✅ Points positifs**:
- Verrouillage automatique avec expiration (auto-unlock)
- Protection contre les superpositions d'affichage
- Gestion correcte du overflow de `millis()` dans `isLocked()`
- API claire : `lockScreen(durationMs)` et `isLocked()`

---

### 10. **Support UTF-8 → CP437**
**Fichier**: `src/display_view.cpp:24-25, 469, 522, etc.`

**✅ Points positifs**:
- Conversion automatique UTF-8 vers CP437 pour l'affichage des accents
- Fonction `DisplayTextUtils::utf8ToCp437()` utilisée systématiquement
- Support des caractères spéciaux (é, è, ç, etc.)

---

## 📊 STATISTIQUES ET MÉTRIQUES

| Métrique | Valeur | État |
|----------|--------|------|
| **Fichiers analysés** | 5 fichiers principaux | ✅ |
| **Problèmes critiques** | 2 | 🔴 |
| **Problèmes moyens** | 4 | 🟡 |
| **Points positifs** | 4 | ✅ |
| **Lignes de code OLED** | ~686 lignes | - |
| **Taux de couverture cache** | 60% (Main + Status, manque Variables) | 🟡 |

---

## 🎯 PRIORITÉS DE CORRECTION

### **PRIORITÉ 1 - URGENT** 🔴
1. **Implémenter `AutomatismDisplayController::updateDisplay()`** 
   - **Impact**: L'OLED ne s'affiche pas du tout en fonctionnement normal
   - **Fichier**: `src/automatism/automatism_display_controller.cpp:43-113`
   - **Action**: Appeler `_display.showMain()`, `_display.showVariables()`, `_display.showCountdown()` selon l'état

2. **Vérifier/déboguer la détection OLED I2C**
   - **Impact**: OLED peut être présente mais non détectée
   - **Fichier**: `src/display_view.cpp:254-265`
   - **Action**: Tester matériel + ajouter retry logic

### **PRIORITÉ 2 - IMPORTANT** 🟡
3. **Unifier la gestion du splash screen**
   - **Impact**: Comportement imprévisible
   - **Fichiers**: `display_view.cpp`, `automatism_display_controller.cpp`, `system_boot.cpp`
   - **Action**: Utiliser uniquement `DisplayConfig::SPLASH_DURATION_MS`

4. **Corriger la gestion de `_isDisplaying` avec timeout**
   - **Impact**: Risque de blocage permanent
   - **Fichier**: `src/display_view.cpp:380-385`
   - **Action**: Ajouter timeout et réinitialisation automatique

5. **Utiliser `getRecommendedDisplayIntervalMs()`**
   - **Impact**: Code mort / duplication
   - **Fichier**: `src/automatism/automatism_display_controller.cpp:34-41, 74-78`
   - **Action**: Utiliser la méthode au lieu du hardcode

### **PRIORITÉ 3 - OPTIMISATION** 🟢
6. **Ajouter `VariablesCache` dans `DisplayCache`**
   - **Impact**: Optimisation performance
   - **Fichier**: `include/display_cache.h`
   - **Action**: Ajouter cache pour `showVariables()`

---

## 📝 RECOMMANDATIONS GÉNÉRALES

### Architecture
1. **Séparation des responsabilités** : `DisplayView` gère l'affichage low-level, `AutomatismDisplayController` gère la logique métier
2. **État centralisé** : Utiliser `AutomatismRuntimeContext` pour passer toutes les données nécessaires
3. **Cache unifié** : Tous les écrans devraient utiliser le cache pour optimiser les performances

### Performance
1. **Rafraîchissement adaptatif** : Réduire la fréquence en mode normal (1000ms), augmenter pendant les animations (250ms)
2. **Batch updates** : Utiliser `beginUpdate()/endUpdate()` pour grouper les modifications
3. **Flush conditionnel** : Utiliser `_needsFlush` pour éviter les appels `display()` inutiles

### Robustesse
1. **Retry logic I2C** : Ajouter 3 tentatives avec délais progressifs lors de l'initialisation
2. **Timeout sur opérations bloquantes** : Toutes les opérations I2C devraient avoir un timeout
3. **Logging détaillé** : Logger tous les états critiques (présence, verrouillage, splash)

---

## ✅ ACTIONS À PRENDRE IMMÉDIATEMENT

1. [ ] **URGENT**: Implémenter `AutomatismDisplayController::updateDisplay()` pour afficher réellement les données
2. [ ] **URGENT**: Vérifier le câblage I2C et tester la détection OLED avec un scanner I2C
3. [ ] Modifier `src/display_view.cpp` pour utiliser `DisplayConfig::SPLASH_DURATION_MS`
4. [ ] Ajouter un timeout sur `_isDisplaying` dans `DisplayView`
5. [ ] Utiliser `getRecommendedDisplayIntervalMs()` dans `updateDisplay()`
6. [ ] Compiler et tester après chaque correction
7. [ ] **Faire un monitoring de 90 secondes** après corrections critiques
8. [ ] Documenter les changements dans `VERSION.md`

---

**Prochaine version recommandée**: v11.120 (correction affichage OLED + détection I2C)

---

*Diagnostic généré le 10 janvier 2026 à partir de l'analyse du code v11.119*

