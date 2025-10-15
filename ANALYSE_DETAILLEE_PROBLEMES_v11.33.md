# 🔍 Analyse Détaillée des Problèmes - Log v11.33

**Date**: 14 Octobre 2025  
**Durée**: ~3 minutes de monitoring  
**Version**: 11.33  

---

## 📊 Synthèse Exécutive

| Catégorie | Sévérité | Nombre | Impact | Action |
|-----------|----------|--------|--------|--------|
| **Capteurs Ultrason HC-SR04** | 🔴 Élevée | ~15 sauts | Valeurs instables | **Diagnostic matériel** |
| **DHT22 Humidité** | 🟡 Moyenne | Constant | Récupération lente | Surveillance |
| **Queue Capteurs** | 🟡 Moyenne | 1 | Perte données | **Résolu v11.34** |
| **Serveur HTTP 500** | 🟢 Faible | 2 | Géré par retry | Surveillance serveur |
| **Valeurs Incohérentes** | 🔴 Élevée | Multiple | Données fausses | **Investigation** |

---

## 🔴 PROBLÈMES CRITIQUES

### 1. **Capteurs Ultrason - Valeurs Aberrantes Systématiques**

#### HC-SR04 Potager - TRÈS INSTABLE ⚠️⚠️⚠️
```
Historique valeurs:
- Ancienne: 129 cm
- Saut vers: 203-209 cm (écart: 74-80 cm)
- Utilise ancienne: 129 cm (par sécurité)

Pattern observé:
Ligne 19: Saut détecté: 129 cm -> 203 cm (écart: 74 cm)
Ligne 56: Saut détecté: 129 cm -> 209 cm (écart: 80 cm)
Ligne 87: Saut détecté: 129 cm -> 208 cm (écart: 79 cm)
```

**Analyse**:
- Valeur "ancienne" figée à **129 cm**
- Lectures réelles autour de **203-209 cm**
- Système rejette systématiquement les vraies lectures !
- **Impact**: Données potager **FAUSSES** dans la BDD

**Cause probable**:
- Initialement une lecture erronée à 129 cm a été acceptée
- Toutes les vraies lectures ensuite sont rejetées comme "sauts"
- **L'algorithme se trompe de référence !**

#### HC-SR04 Aquarium - INSTABLE ⚠️⚠️
```
Pattern observé:
Ligne 25: Saut détecté: 173 cm -> 209 cm (écart: 36 cm)
Ligne 63: Saut détecté: 173 cm -> 209 cm (écart: 36 cm)
Ligne 99: Saut détecté: 173 cm -> 209 cm (écart: 36 cm)

Mais ligne 38: actuel=173 (accepté!)
```

**Analyse**:
- Oscille entre **173 cm** et **209 cm**
- Saut de **36 cm** répété
- Parfois 173 accepté, parfois rejeté
- **Pattern suspect**: toujours exactement 36 cm d'écart

#### HC-SR04 Réserve - INSTABLE ⚠️⚠️
```
Pattern observé:
Ligne 33: Saut détecté: 123 cm -> 209 cm (écart: 86 cm)
Ligne 70: Saut détecté: 123 cm -> 209 cm (écart: 86 cm)
Ligne 108: Saut détecté: 123 cm -> 210 cm (écart: 87 cm)
```

**Analyse**:
- Valeur ancienne figée à **123 cm**
- Lectures réelles **209-210 cm**
- Même problème que potager
- **Impact**: Données réserve **FAUSSES**

### 2. **Logique Filtrage HC-SR04 Défaillante** 🚨

**Problème identifié**:
```cpp
// L'algorithme actuel semble faire:
1. Une "mauvaise" lecture est acceptée (129 cm, 123 cm, 173 cm)
2. Elle devient la référence
3. Toutes les vraies lectures sont rejetées comme "sauts"
4. Résultat: données fausses envoyées au serveur !
```

**Preuve dans payload** (ligne 40):
```
EauPotager=129   ← FAUX (devrait être ~209)
EauAquarium=173  ← Potentiellement faux
EauReserve=123   ← FAUX (devrait être ~209)
```

**Impact Business**:
- ❌ **Alertes inondation fausses** possibles
- ❌ **Décisions automatiques basées sur données erronées**
- ❌ **Graphiques/historique corrompus**

---

## 🟡 PROBLÈMES MOYENS

### 3. **DHT22 - Filtrage Échoue Systématiquement**

```
Pattern constant:
[AirSensor] Filtrage avancé échoué, tentative de récupération...
[AirSensor] Tentative de récupération 1/2...
[AirSensor] Récupération réussie: 62.0%
```

**Occurrences**: Lignes 11-14, 43-51, 66-82

**Analyse**:
- Filtrage avancé ne fonctionne **jamais**
- Récupération réussit **toujours**
- Valeur humidité **figée à 62.0%** (suspect!)
- Durée lecture: **3.3 secondes** (long mais acceptable)

**Questions**:
- Pourquoi filtrage échoue systématiquement ?
- Pourquoi humidité toujours exactement 62.0% ?
- Capteur défectueux ou code de filtrage trop strict ?

### 4. **Queue Capteurs Saturée** (Résolu v11.34)

```
Ligne 113: [Sensor] ⚠️ Queue pleine - donnée de capteur perdue!
```

**Contexte**: Survient pendant retry HTTP 500 (lignes 91-119)
- Retry 2/3 en cours (200ms wait)
- Retry 3/3 suit (800ms wait)
- Lectures capteurs continuent (5-7s)
- **Queue 10 slots saturée**

**Status**: ✅ Déjà corrigé en v11.34 (30 slots + timeout 200ms)

---

## 🟢 PROBLÈMES MINEURS

### 5. **Erreurs Serveur HTTP 500**

```
Ligne 91-95: HTTP 500 "Erreur serveur"
Ligne 114-118: HTTP 500 "Erreur serveur" (retry)
```

**Analyse**:
- Endpoint ACK serveur: `/post-data-test`
- Payload: ACK pompe tank off
- **Problème côté serveur**, pas ESP32
- Retry automatique fonctionne ✓

**Impact**: Aucun (ESP32 gère correctement)

### 6. **Température Air Lecture 0 ms**

```
Ligne 10: [SystemSensors] ⏱️ Température air: 0 ms
Ligne 124: [SystemSensors] ⏱️ Température air: 1 ms
```

**Analyse**:
- Lecture presque instantanée (cache ?)
- Ou valeur non mise à jour
- Pas d'erreur signalée
- Valeur finale: 27.7°C (cohérente)

**Impact**: Négligeable

### 7. **Marée - Changements Brutaux**

```
Ligne 5: diff15s=-36 cm (chute brutale)
Ligne 32: diff15s=-36 cm (persiste)
Ligne 38: actuel=173, diff15s=0 cm (stabilisation)
Ligne 61: diff15s=-36 cm (rechute)
```

**Analyse**:
- Variations de **36 cm en 15 secondes** !
- Correspond aux sauts HC-SR04 aquarium
- **Conséquence directe des valeurs capteurs fausses**

**Impact**: 
- Détection marée erronée
- Sleep précoce possible (fausse marée montante)

---

## 🎯 RECOMMANDATIONS URGENTES

### 1. **Corriger Algorithme Filtrage HC-SR04** 🚨 PRIORITÉ 1

**Problème**:
```cpp
// Logique actuelle (hypothèse):
if (abs(nouvelle - ancienne) > SEUIL) {
    utiliser_ancienne();  // ❌ PROBLÈME
}
```

**Solution proposée**:
```cpp
// Améliorer avec médiane glissante:
1. Buffer des N dernières lectures (ex: 10)
2. Calculer médiane du buffer
3. Si nouvelle lecture proche médiane → accepter
4. Si écart > seuil → vérifier cohérence avec médiane
5. Si médiane change brutalement → réinitialiser buffer

// Ou alternativement:
1. Si 3 lectures consécutives concordent → accepter nouvelle référence
2. Ne pas "figer" indéfiniment une valeur aberrante
```

**Exemple code**:
```cpp
const uint8_t BUFFER_SIZE = 10;
uint16_t buffer[BUFFER_SIZE];
uint8_t bufferIndex = 0;

void updateValue(uint16_t newReading) {
    buffer[bufferIndex] = newReading;
    bufferIndex = (bufferIndex + 1) % BUFFER_SIZE;
    
    // Calculer médiane du buffer
    uint16_t median = calculateMedian(buffer, BUFFER_SIZE);
    
    // Si nouvelle lecture proche médiane (±20cm) → OK
    if (abs(newReading - median) < 20) {
        currentValue = median;
    }
    // Sinon, attendre confirmation
}
```

### 2. **Investiguer DHT22 Humidité Figée** ⚠️ PRIORITÉ 2

**Actions**:
1. Vérifier pourquoi filtrage échoue systématiquement
2. Vérifier pourquoi valeur toujours exactement 62.0%
3. Tester avec capteur de remplacement
4. Logger les valeurs brutes avant filtrage

### 3. **Ajouter Détection Valeurs Figées** 🛡️ PRIORITÉ 3

**Code proposé**:
```cpp
// Détecter si capteur retourne toujours la même valeur
class StuckValueDetector {
    uint16_t lastValue;
    uint8_t sameCount;
    
    bool isStuck(uint16_t newValue, uint8_t threshold = 10) {
        if (newValue == lastValue) {
            sameCount++;
            if (sameCount >= threshold) {
                Serial.println("⚠️ Capteur figé détecté!");
                return true;
            }
        } else {
            sameCount = 0;
        }
        lastValue = newValue;
        return false;
    }
};
```

### 4. **Améliorer Logs Diagnostics** 📊 PRIORITÉ 4

**Ajouter**:
```cpp
// Pour chaque capteur ultrason
Serial.printf("[Ultrasonic] DIAGNOSTIC:\n");
Serial.printf("  - Lectures brutes: %d, %d, %d cm\n", r1, r2, r3);
Serial.printf("  - Médiane: %d cm\n", median);
Serial.printf("  - Ancienne valeur: %d cm\n", oldValue);
Serial.printf("  - Historique (10 dernières): [%s]\n", bufferStr);
Serial.printf("  - Décision: %s\n", decision);
```

---

## 📊 Impact Global

### Données Envoyées au Serveur (FAUSSES!)

```
Payload ligne 40:
EauPotager=129    ← FAUX (vraie valeur: ~209 cm)
EauAquarium=173   ← SUSPECT (oscille 173/209)
EauReserve=123    ← FAUX (vraie valeur: ~209 cm)
diffMaree=0       ← Conséquence des valeurs fausses
```

**Conséquences**:
- ❌ Base de données corrompue
- ❌ Graphiques historiques faux
- ❌ Alertes basées sur données erronées
- ❌ Décisions automatiques potentiellement dangereuses

### Intégrité des Données

| Capteur | Fiabilité | Données BDD | Action |
|---------|-----------|-------------|--------|
| **Température eau** | ✅ 100% | Correctes | Aucune |
| **Température air** | ✅ 100% | Correctes | Aucune |
| **Humidité** | ⚠️ 50% | Suspectes (figées?) | Investigation |
| **HC-SR04 Potager** | ❌ 0% | **FAUSSES** | **Correction urgente** |
| **HC-SR04 Aquarium** | ⚠️ 30% | Erratiques | **Correction urgente** |
| **HC-SR04 Réserve** | ❌ 0% | **FAUSSES** | **Correction urgente** |
| **Luminosité** | ✅ 100% | Correctes | Aucune |

---

## 🎯 Plan d'Action Prioritaire

### Phase 1: URGENT (Aujourd'hui)
1. ✅ **v11.34**: Queue capteurs (déjà fait)
2. 🚨 **v11.35**: Corriger algorithme filtrage HC-SR04
   - Implémenter médiane glissante
   - Détection valeurs figées
   - Logs diagnostics améliorés

### Phase 2: IMPORTANT (Cette semaine)
3. 🔍 Investiguer DHT22 humidité figée à 62%
4. 🧪 Tester capteurs HC-SR04 avec multimètre
5. 📊 Nettoyer données BDD erronées (historique)

### Phase 3: PRÉVENTIF (Ce mois)
6. 🛡️ Implémenter watchdog capteurs (valeurs figées)
7. 📈 Ajouter métriques qualité données
8. 🔔 Alertes capteurs défaillants

---

## 📝 Conclusion

### Statut Système

**Software v11.33**:
- ✅ Watchdog: Stable
- ✅ NVS: Stable
- ✅ Mémoire: Stable
- ⚠️ Queue: Saturée 1x (résolu v11.34)

**Capteurs**:
- ✅ DS18B20: Excellent
- ✅ Température air: Bon
- ⚠️ DHT22 humidité: Suspect (figé?)
- ❌ **HC-SR04 (×3): DÉFAILLANTS** (logique ou matériel)

**Données**:
- ✅ Températures: Fiables
- ⚠️ Humidité: Suspecte
- ❌ **Niveaux eau: NON FIABLES** ⚠️⚠️⚠️

### Verdict Final

🔴 **SYSTÈME NON FIABLE POUR PRODUCTION**

**Raison**: Les capteurs HC-SR04 génèrent des **données fausses** qui sont envoyées au serveur et stockées en BDD.

**Action immédiate requise**: Corriger algorithme filtrage HC-SR04 (v11.35)

---

**Version analysée**: 11.33  
**Date**: 14 Octobre 2025  
**Prochaine version**: 11.35 (fix algorithme HC-SR04)


