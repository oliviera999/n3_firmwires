# Séparation Activité Web et forceWakeUp (GPIO 115) - v11.21

**Date**: 2025-10-13  
**Version**: 11.21  
**Type**: Amélioration comportement sleep

---

## 📋 Problème Identifié

### Comportement Problématique (avant v11.21)

Lorsqu'un utilisateur ouvrait l'interface web locale de l'ESP32:

1. **Activation automatique**: `notifyLocalWebActivity()` activait automatiquement `forceWakeUp = true`
2. **Sauvegarde persistante**: La valeur était sauvegardée dans la NVS via `_config.setForceWakeUp()` et `saveBouffeFlags()`
3. **Synchronisation BDD**: Au prochain cycle, `forceWakeUp=1` était envoyé au serveur distant (GPIO 115)
4. **Persistance après reboot**: Même après redémarrage ESP32, `forceWakeUp` restait à `1` (restauré depuis NVS)

### Conséquences

- ❌ **Désynchronisation**: ESP32 affichait `forceWakeUp=1` alors que BDD distante avait `GPIO 115=0`
- ❌ **Comportement non intuitif**: L'utilisateur n'avait pas demandé d'activer le mode veille forcé
- ❌ **Confusion**: Impossible de savoir si forceWakeUp était activé volontairement ou par l'ouverture de l'interface

### Demande Utilisateur

> "Ne PAS activer automatiquement forceWakeUp mais juste empêcher le sleep temporairement"

---

## 🎯 Solution Implémentée

### Principe

**Séparation complète des deux mécanismes**:

1. **Blocage temporaire du sleep** (activité web):
   - Empêche le sleep pendant consultation interface
   - Timeout: 10 minutes
   - **NON persistant** (disparaît après timeout ou reboot)
   - **N'affecte PAS** GPIO 115

2. **forceWakeUp persistant** (GPIO 115):
   - Contrôlé **UNIQUEMENT** par action explicite utilisateur ou serveur distant
   - Sauvegardé dans NVS (persistant après reboot)
   - Synchronisé avec BDD distante

---

## 🔧 Modifications Techniques

### 1. Modification de `notifyLocalWebActivity()`

**Fichier**: `src/automatism/automatism_sleep.cpp` (lignes 67-75)

#### Avant (v11.20)
```cpp
void AutomatismSleep::notifyLocalWebActivity() {
    _lastWebActivityMs = millis();
    _forceWakeFromWeb = true;
    
    if (!_forceWakeUp) {
        _forceWakeUp = true;  // ❌ Activation automatique
        Serial.println(F("[Sleep] forceWakeUp activé par activité web locale"));
        _config.setForceWakeUp(_forceWakeUp);  // ❌ Sauvegarde persistante
        _config.saveBouffeFlags();
    }
}
```

#### Après (v11.21)
```cpp
void AutomatismSleep::notifyLocalWebActivity() {
    _lastWebActivityMs = millis();
    _forceWakeFromWeb = true;
    
    // NOTE: On n'active PLUS forceWakeUp automatiquement
    // On empêche juste le sleep temporairement pendant la consultation
    // forceWakeUp (GPIO 115) doit être contrôlé explicitement par l'utilisateur
    Serial.println(F("[Sleep] Activité web détectée - sleep bloqué temporairement (10 min)"));
}
```

**Changements**:
- ✅ Ne modifie plus `_forceWakeUp`
- ✅ Ne sauvegarde plus dans NVS
- ✅ Log explicite du comportement

---

### 2. Refactoring de `handleAutoSleep()`

**Fichier**: `src/automatism.cpp` (lignes 2044-2078)

#### Avant (v11.20)
```cpp
void Automatism::handleAutoSleep(const SensorReadings& r) {
  // Sommeil forcé désactivé ?
  if (forceWakeUp) {
    // Si le wake est maintenu par activité web, vérifier le timeout
    if (_forceWakeFromWeb) {
      unsigned long now = millis();
      if (timeout) {
        _forceWakeFromWeb = false;
        forceWakeUp = false; // ❌ Libération automatique de forceWakeUp
        _config.setForceWakeUp(forceWakeUp);
        _config.saveBouffeFlags();
      } else {
        return; // Bloquer sleep
      }
    }
    return; // forceWakeUp actif
  }
  // ... reste du code
}
```

**Problème**: La logique mélangeait activité web et forceWakeUp persistant

#### Après (v11.21)
```cpp
void Automatism::handleAutoSleep(const SensorReadings& r) {
  // ========================================
  // 1. VÉRIFICATION ACTIVITÉ WEB TEMPORAIRE (NON PERSISTANTE)
  // Empêche le sleep pendant consultation interface web, sans modifier GPIO 115
  // ========================================
  if (_forceWakeFromWeb) {
    unsigned long now = millis();
    if (_lastWebActivityMs > 0 && (now - _lastWebActivityMs > TimingConfig::WEB_ACTIVITY_TIMEOUT_MS)) {
      // Timeout atteint -> libérer le blocage temporaire
      _forceWakeFromWeb = false;
      Serial.println(F("[Auto] Activité web expirée - sleep autorisé à nouveau"));
    } else {
      // Activité web récente -> empêcher sleep temporairement
      static unsigned long lastWebLog = 0;
      unsigned long currentMillis = millis();
      if (currentMillis - lastWebLog > 30000) {
        lastWebLog = currentMillis;
        Serial.println(F("[Auto] Auto-sleep bloqué temporairement (activité web récente)"));
      }
      return; // ⚠️ Bloquer sleep mais ne pas toucher forceWakeUp
    }
  }
  
  // ========================================
  // 2. VÉRIFICATION FORCEWAKEUP PERSISTANT (GPIO 115)
  // Contrôlé UNIQUEMENT par l'utilisateur via toggle ou serveur distant
  // ========================================
  if (forceWakeUp) {
    static unsigned long lastWakeUpLog = 0;
    unsigned long currentMillis = millis();
    if (currentMillis - lastWakeUpLog > 30000) {
      lastWakeUpLog = currentMillis;
      Serial.println(F("[Auto] Auto-sleep désactivé (forceWakeUp GPIO 115 = true)"));
    }
    return;
  }
  
  // ... reste du code (vérifications critiques, etc.)
}
```

**Changements**:
- ✅ **Séparation en 2 blocs distincts** et séquentiels
- ✅ Activité web vérifiée en **PREMIER** (priorité)
- ✅ forceWakeUp vérifié en **SECOND** (indépendant de l'activité web)
- ✅ Plus aucune modification automatique de `forceWakeUp`
- ✅ Logs explicites pour chaque cas

---

## 📊 Comportement Nouveau

### Scénario 1: Ouverture Interface Web

**Actions**:
1. Utilisateur ouvre `http://192.168.1.XX/` dans un navigateur
2. Requête HTTP → `autoCtrl.notifyLocalWebActivity()` appelé

**Résultats**:
```
[Sleep] Activité web détectée - sleep bloqué temporairement (10 min)
[Auto] Auto-sleep bloqué temporairement (activité web récente)
```

**États**:
- ✅ `_forceWakeFromWeb = true` (temporaire)
- ✅ `_lastWebActivityMs = millis()` (timestamp)
- ✅ `forceWakeUp` **INCHANGÉ** (reste à sa valeur précédente)
- ✅ GPIO 115 **INCHANGÉ** dans BDD distante

**Durée**: 10 minutes (ou jusqu'à nouvelle activité web)

---

### Scénario 2: Fermeture Onglet / Inactivité

**Actions**:
1. Utilisateur ferme l'onglet (ou 10 minutes d'inactivité)
2. Timeout atteint dans `handleAutoSleep()`

**Résultats**:
```
[Auto] Activité web expirée - sleep autorisé à nouveau
```

**États**:
- ✅ `_forceWakeFromWeb = false` (libéré)
- ✅ Sleep à nouveau autorisé (si autres conditions OK)
- ✅ `forceWakeUp` **INCHANGÉ**
- ✅ GPIO 115 **INCHANGÉ**

---

### Scénario 3: Toggle forceWakeUp Explicite

**Actions**:
1. Utilisateur clique sur bouton "Force Wakeup" dans interface
2. `autoCtrl.toggleForceWakeup()` appelé

**Résultats**:
```
[Actuators] 🔓 Toggle Force Wakeup: OFF → ON
[Auto] ✅ Synchronisation serveur réussie
```

**États**:
- ✅ `forceWakeUp = true` (persistant)
- ✅ Sauvegardé dans NVS via `_config.setForceWakeUp()`
- ✅ Envoyé au serveur distant (`forceWakeUp=1`)
- ✅ GPIO 115 = 1 dans BDD distante
- ✅ Persistera après reboot ESP32

---

### Scénario 4: Reboot ESP32

**Actions**:
1. ESP32 redémarre (reset, power cycle, etc.)
2. `Automatism::begin()` restaure état depuis NVS

**Résultats**:
```
[Auto] forceWakeUp restauré depuis config: true  (ou false selon dernière valeur)
```

**États**:
- ✅ `_forceWakeFromWeb = false` (remis à zéro)
- ✅ `_lastWebActivityMs = 0` (remis à zéro)
- ✅ `forceWakeUp` **restauré depuis NVS** (valeur persistante)
- ✅ Cohérence avec BDD distante

---

## 🔍 Logs de Diagnostic

### Activité Web Détectée
```
[Sleep] Activité web détectée - sleep bloqué temporairement (10 min)
[Auto] Auto-sleep bloqué temporairement (activité web récente)
```
**Signification**: Interface web ouverte, sleep temporairement bloqué, forceWakeUp non modifié

---

### Timeout Activité Web
```
[Auto] Activité web expirée - sleep autorisé à nouveau
```
**Signification**: 10 minutes sans activité web, blocage temporaire levé, sleep à nouveau possible

---

### forceWakeUp Persistant Actif
```
[Auto] Auto-sleep désactivé (forceWakeUp GPIO 115 = true)
```
**Signification**: Mode veille forcé activé explicitement par utilisateur ou serveur, sleep désactivé de manière persistante

---

## ✅ Avantages de la Solution

1. **Clarté**:
   - Distinction nette entre blocage temporaire et persistant
   - Logs explicites pour chaque cas

2. **Cohérence**:
   - forceWakeUp (GPIO 115) synchronisé avec BDD distante
   - Pas de désynchronisation surprenante

3. **Intuitivité**:
   - Ouverture interface → sleep bloqué temporairement (attendu)
   - forceWakeUp modifié **uniquement** par action explicite (attendu)

4. **Persistance contrôlée**:
   - Activité web: **NON persistante** (disparaît après timeout ou reboot)
   - forceWakeUp: **Persistante** (survit au reboot)

5. **Économie d'énergie**:
   - Sleep reprend automatiquement après 10 min d'inactivité
   - forceWakeUp reste contrôlé manuellement

---

## 🧪 Tests à Effectuer

### Test 1: Activité Web Temporaire
1. ✅ Ouvrir interface web
2. ✅ Vérifier log: `[Sleep] Activité web détectée - sleep bloqué temporairement`
3. ✅ Vérifier que `forceWakeUp` reste inchangé dans `/api/status`
4. ✅ Fermer onglet et attendre 10 minutes
5. ✅ Vérifier log: `[Auto] Activité web expirée - sleep autorisé à nouveau`

### Test 2: Toggle forceWakeUp Explicite
1. ✅ Cliquer sur bouton "Force Wakeup" dans interface
2. ✅ Vérifier log: `[Actuators] Toggle Force Wakeup`
3. ✅ Vérifier `/api/status`: `forceWakeup: true`
4. ✅ Vérifier BDD distante: GPIO 115 = 1

### Test 3: Reboot avec forceWakeUp=1
1. ✅ Activer forceWakeUp via interface
2. ✅ Redémarrer ESP32
3. ✅ Vérifier log: `[Auto] forceWakeUp restauré depuis config: true`
4. ✅ Vérifier `/api/status`: `forceWakeup: true`

### Test 4: Reboot sans forceWakeUp
1. ✅ Désactiver forceWakeUp via interface
2. ✅ Ouvrir interface (activité web)
3. ✅ Redémarrer ESP32 **immédiatement** (sans attendre timeout)
4. ✅ Vérifier log: `[Auto] forceWakeUp restauré depuis config: false`
5. ✅ Vérifier `/api/status`: `forceWakeup: false`
6. ✅ Confirmer: Activité web n'a pas persisté

---

## 📄 Fichiers Modifiés

| Fichier | Lignes | Description |
|---------|--------|-------------|
| `src/automatism/automatism_sleep.cpp` | 67-75 | Suppression activation automatique forceWakeUp |
| `src/automatism.cpp` | 2044-2078 | Séparation logique web temporaire vs forceWakeUp persistant |
| `include/project_config.h` | 27 | Version incrémentée à 11.21 |
| `VERSION.md` | 1-60 | Documentation changements v11.21 |

---

## 🎯 Conclusion

Cette modification sépare proprement:
- **Activité web** → Blocage temporaire du sleep (10 min, non persistant)
- **forceWakeUp (GPIO 115)** → Contrôle explicite persistant

L'utilisateur a désormais:
- ✅ Consultation interface sans effet de bord sur GPIO 115
- ✅ Contrôle explicite de forceWakeUp via toggle ou serveur distant
- ✅ Cohérence ESP32 ↔ BDD distante
- ✅ Comportement prévisible et documenté

**Tests restants**: Valider sur hardware réel les 4 scénarios ci-dessus.

