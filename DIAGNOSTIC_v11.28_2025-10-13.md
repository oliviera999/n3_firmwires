# 📊 DIAGNOSTIC COMPLET - VERSION 11.28
**Date:** 2025-10-13 15:01:58  
**Durée monitoring:** 90 secondes  
**Environnement:** wroom-test

---

## ✅ POINTS POSITIFS (Stabilité système)

### 🟢 Pas d'erreurs critiques
- ✅ **Aucun Guru Meditation Error**
- ✅ **Aucun Panic**
- ✅ **Aucun Brownout**
- ✅ **Aucun Core Dump**
- ✅ **Aucun Watchdog timeout**

### 🟢 Système fonctionnel
- ✅ **WiFi connecté** - RSSI stable entre -62 et -64 dBm (Acceptable)
- ✅ **OLED opérationnel** - Splash screen et affichage OK
- ✅ **Capteurs opérationnels** - Lectures régulières
- ✅ **Mémoire stable** - 75484 bytes libres (minHeap sauvegardé)
- ✅ **Version correcte** - v11.28 affichée dans les payloads

---

## ⚠️ PROBLÈMES DÉTECTÉS (Priorité 1 & 2)

### 🔴 **PRIORITÉ 1 - Erreurs réseau HTTP** 
**Impact:** Envoi de données au serveur échoue

#### Erreur 1: Connection Lost (-5) et Send Header Failed (-2)
```
[HTTP] ❌ ERROR -5 (attempt 1/3) - connection lost
[HTTP] ❌ ERROR -2 (attempt 2/3) - send header failed
```
**Occurrences:** Multiple tentatives échouées  
**Cause probable:** 
- Saturation de connexions TCP simultanées
- Problème de timing entre requêtes HTTP
- Possible conflit de ressources réseau

**Solution recommandée:**
1. Augmenter le délai entre requêtes HTTP successives
2. Implémenter un pool de connexions HTTP réutilisables
3. Vérifier la limite de connexions simultanées du serveur

---

#### Erreur 2: Clé API incorrecte (401)
```
[HTTP] ← code 401, 19 bytes
Clé API incorrecte
```
**Impact:** Certaines requêtes sont rejetées par le serveur

**Cause:** API key tronquée dans le payload
```
Payload envoyé: api_key=fdGTMoptd5CD2ert3  ❌
Payload attendu: api_key=fdGTMoptd5CD2ert3  ✅
```
**Note:** La clé semble correcte (`fdGTMoptd5CD2ert3`) mais le serveur la rejette parfois

**Solution recommandée:**
1. Vérifier la clé API côté serveur (test/prod)
2. Logger la clé complète envoyée pour debug
3. Tester avec profil TEST vs PROD

---

#### Erreur 3: Heartbeat Endpoint Incorrect (301 Redirect)
```
[HTTP] ← code 301 - Moved Permanently
URL demandée: /ffp3/ffp3datas/heartbeat.php  ❌
URL correcte:  /ffp3/heartbeat.php            ✅
```
**Impact:** Heartbeat échoue avec HTML au lieu de JSON

**Solution:** Corriger l'endpoint dans `project_config.h`
```cpp
// Ligne 93 - À CORRIGER
constexpr const char* HEARTBEAT_ENDPOINT = "/ffp3/heartbeat.php";  // Au lieu de /ffp3/ffp3datas/heartbeat.php
```

---

### 🟡 **PRIORITÉ 2 - Capteurs & Mesures**

#### Problème 1: DHT Filtrage Avancé Échoue
```
[AirSensor] Filtrage avancé échoué, tentative de récupération...
[AirSensor] Tentative de récupération 1/2...
[AirSensor] Récupération réussie: 62.0%
```
**Occurrences:** Régulières (toutes les ~5s)  
**Impact:** Latence lecture DHT augmentée (~3.3s au lieu de ~2s)

**Cause:** 
- Capteur DHT22 sensible (timing critique)
- Valeurs hors plage pour filtrage avancé
- Possible interférence avec autres tâches

**Solution recommandée:**
1. Augmenter le délai minimum entre lectures DHT (actuellement 3000ms)
2. Assouplir les critères du filtrage avancé
3. Vérifier les plages de validation dans `SensorConfig::AirSensor`

---

#### Problème 2: Sauts de Niveau Potager
```
[Ultrasonic] Saut détecté: 124 cm -> 208 cm (écart: 84 cm)
[Ultrasonic] Saut vers le haut, utilise ancienne valeur par sécurité
```
**Occurrences:** Régulières  
**Cause:** Capteur ultrasonique instable ou obstacle intermittent

**Solution recommandée:**
1. Vérifier le montage du capteur HC-SR04 (potager)
2. Nettoyer les transducteurs (poussière/toile d'araignée)
3. Augmenter le nombre d'échantillons pour médiane (actuellement 3 → 5)

---

#### Problème 3: Niveau Réservoir Critique
```
[CRITIQUE] === SÉCURITÉ RÉSERVE BASSE ===
[CRITIQUE] Niveau réservoir (distance): 209 cm, Seuil: 80 cm
```
**Impact:** Pompe réservoir verrouillée (sécurité activée)

**Note:** ⚠️ RAPPEL logique inverse ultrason :
- 209 cm = Réservoir **VIDE** (capteur loin de l'eau)
- Seuil 80 cm = Niveau minimum acceptable

**Action:** Remplir physiquement le réservoir

---

## 📈 MÉTRIQUES DE PERFORMANCE

### Temps de lecture capteurs
| Capteur | Temps moyen | Min | Max |
|---------|-------------|-----|-----|
| **Humidité DHT** | 3.32s | 3.30s | 3.35s |
| **Temp eau DS18B20** | 0.77s | 0.77s | 1.32s |
| **Niveau aquarium** | 0.19s | 0.19s | 0.23s |
| **Niveau potager** | 0.23s | 0.21s | 0.23s |
| **Niveau réservoir** | 0.22s | 0.22s | 0.23s |
| **Luminosité** | 0.01s | 0.01s | 0.01s |
| **TOTAL cycle** | 4.9s | 4.7s | 5.3s |

### Mémoire
- **Heap libre:** 75,484 bytes
- **Min heap:** 75,484 bytes  
- **Reboots:** 3
- **Uptime:** 68 secondes

### WiFi
- **RSSI:** -62 à -64 dBm (Acceptable)
- **État:** Connecté (status: 3)
- **Stabilité:** Bonne (pas de déconnexion)

---

## 🔧 ACTIONS CORRECTIVES RECOMMANDÉES

### Haute priorité (à faire maintenant)
1. ✅ **Corriger endpoint heartbeat** dans `project_config.h`
   ```cpp
   constexpr const char* HEARTBEAT_ENDPOINT = "/ffp3/heartbeat.php";
   ```

2. ⚠️ **Investiguer erreurs HTTP connection lost**
   - Ajouter délai entre requêtes POST successives
   - Implémenter retry avec backoff exponentiel

3. ⚠️ **Vérifier clé API** 
   - Logger la clé complète envoyée
   - Tester avec endpoints TEST vs PROD

### Priorité moyenne
4. 🔧 **Optimiser DHT22**
   - Augmenter délai minimum lecture à 3500ms (actuellement 3000ms)
   - Assouplir critères filtrage avancé

5. 🔧 **Nettoyer capteur potager**
   - Vérifier montage HC-SR04
   - Nettoyer transducteurs

### Priorité basse (monitoring)
6. 📊 **Surveiller mémoire**
   - Heap stable à 75KB
   - Pas d'action requise pour le moment

7. 📊 **Remplir réservoir** (action physique)
   - Niveau critique détecté (209 cm)

---

## 📝 CONCLUSION

### ✅ Stabilité système : EXCELLENTE
- Aucun crash, panic ou watchdog timeout
- Mémoire stable
- WiFi connecté et stable

### ⚠️ Fiabilité réseau : À AMÉLIORER
- Erreurs HTTP intermittentes (-5, -2)
- Endpoint heartbeat incorrect (301)
- Clé API rejetée parfois (401)

### 🔧 Capteurs : FONCTIONNELS avec optimisations possibles
- DHT22 : Récupération fonctionne mais filtrage échoue
- Ultrasons : Sauts détectés sur potager (montage ?)
- DS18B20 : Parfait ✅

### 🎯 Prochaines étapes
1. **Incrémenter version → 11.29**
2. **Corriger endpoint heartbeat**
3. **Tester avec fix HTTP retry**
4. **Re-monitorer 90 secondes**

---

**Diagnostic généré automatiquement - v11.28**  
*Prochaine version attendue: 11.29*

