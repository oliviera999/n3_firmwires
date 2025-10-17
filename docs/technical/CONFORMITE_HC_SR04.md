# Conformité aux recommandations HC-SR04

## Résumé de l'audit

Ce document détaille l'analyse de conformité du code de gestion des capteurs ultrasoniques HC-SR04 par rapport aux spécifications officielles de la datasheet.

---

## ✅ Points conformes

### 1. Séquence de déclenchement (Trigger)
**Recommandation datasheet** : Pulse HIGH de minimum 10µs précédé d'un LOW de 2µs

**Implémentation** : ✅ CONFORME
```cpp
digitalWrite(_pinTrigEcho, LOW);
delayMicroseconds(2);
digitalWrite(_pinTrigEcho, HIGH);
delayMicroseconds(10);
digitalWrite(_pinTrigEcho, LOW);
```

### 2. Timeout de mesure
**Recommandation datasheet** : Suffisant pour couvrir la distance maximale
- Distance max : 400cm
- Temps théorique : ~23.5ms (aller-retour)

**Implémentation** : ✅ CONFORME
```cpp
constexpr uint32_t TIMEOUT_US = 30000; // 30ms
```

### 3. Facteur de conversion
**Recommandation datasheet** : ~58µs/cm (vitesse du son ~340m/s)

**Implémentation** : ✅ CONFORME
```cpp
uint16_t cm = duration / 58;
```

### 4. Plage de mesure
**Recommandation datasheet** : 2cm - 400cm

**Implémentation** : ✅ CONFORME (après correction)
```cpp
constexpr uint16_t MIN_DISTANCE_CM = 2;
constexpr uint16_t MAX_DISTANCE_CM = 400;
```

---

## ⚠️ Points corrigés

### 1. Délai de stabilisation SETTLE_TIME_US
**Problème identifié** : Délai de 200µs après passage en mode INPUT
- ❌ **Non documenté dans la datasheet** 
- ⚠️ **Peut causer la perte du début du pulse d'écho**

**Correction appliquée** : ✅ Suppression du délai
```cpp
// AVANT (incorrect)
pinMode(_pinTrigEcho, INPUT);
delayMicroseconds(SETTLE_TIME_US); // 200µs - NON RECOMMANDÉ
unsigned long duration = readEchoPulseUs(TIMEOUT_US);

// APRÈS (conforme)
pinMode(_pinTrigEcho, INPUT);
unsigned long duration = readEchoPulseUs(TIMEOUT_US);
```

**Justification** : Le HC-SR04 émet le pulse d'écho immédiatement après le pulse de déclenchement. Attendre 200µs peut faire manquer le début du signal, faussant la mesure.

### 2. Délai entre mesures
**Recommandation datasheet** : Minimum **60ms** entre deux mesures consécutives

**Problème identifié** : Calcul dynamique basé sur DEFAULT_SAMPLES
```cpp
// AVANT (potentiellement problématique)
vTaskDelay(pdMS_TO_TICKS(SensorConfig::Ultrasonic::DEFAULT_SAMPLES * 20)); 
// = 5 * 20 = 100ms (OK) mais logique incorrecte
```

**Correction appliquée** : ✅ Valeur fixe conforme
```cpp
// APRÈS (conforme et clair)
vTaskDelay(pdMS_TO_TICKS(60)); // Délai minimum recommandé par datasheet HC-SR04
```

**Justification** : 
- Le délai entre mesures doit être **constant** et ne dépend pas du nombre d'échantillons
- Évite les échos résiduels et les interférences entre mesures
- 60ms est le **minimum recommandé** par le fabricant

### 3. Distances min/max initiales
**Problème identifié** : Valeurs non conformes à la datasheet
```cpp
// AVANT
MIN_DISTANCE = 0;   // Hors spécification
MAX_DISTANCE = 500; // Au-delà de la portée du capteur
```

**Correction appliquée** : ✅ Valeurs conformes
```cpp
// APRÈS
MIN_DISTANCE = 2;   // Minimum datasheet HC-SR04
MAX_DISTANCE = 400; // Maximum datasheet HC-SR04
```

---

## 📋 Recommandations supplémentaires

### 1. Configuration pin TRIG/ECHO séparés
**État actuel** : Mode 1-pin (TRIG et ECHO partagés)
```cpp
UltrasonicManager(int pinTrigEcho); // Un seul pin
```

**Recommandation** : Vérifier le câblage physique
- Le HC-SR04 **standard** utilise 2 pins séparés
- Certains modules/clones permettent le mode 1-pin
- **Si vous utilisez un HC-SR04 standard** : modifier pour 2 pins

**Implémentation suggérée** (si nécessaire) :
```cpp
class UltrasonicManager {
public:
  UltrasonicManager(int pinTrig, int pinEcho);
  // ...
private:
  int _pinTrig;
  int _pinEcho;
};
```

### 2. Gestion des erreurs de mesure
**État actuel** : ✅ Bon filtrage avec médiane et historique

Points forts :
- Filtrage par médiane sur 3 lectures
- Historique glissant pour détecter les aberrations
- Fallback sur dernière valeur valide

### 3. Protection du watchdog
**État actuel** : ✅ Excellent
```cpp
esp_task_wdt_reset(); // Avant chaque mesure
```

---

## 🔧 Tests recommandés

Après les corrections, vérifier :

1. **Précision des mesures**
   - Tester à différentes distances (2cm, 50cm, 200cm, 400cm)
   - Comparer avec un mètre physique

2. **Stabilité**
   - Observer les lectures sur plusieurs minutes
   - Vérifier l'absence de dérives ou sauts erratiques

3. **Performance**
   - Temps de réponse du système
   - Impact sur les autres tâches (affichage, WiFi)

4. **Cas limites**
   - Objets absorbants (tissu, mousse)
   - Surfaces angulées
   - Distances très courtes (<5cm)

---

## 📊 Résumé des modifications

| Paramètre | Avant | Après | Conformité |
|-----------|-------|-------|------------|
| Pulse trigger | 10µs | 10µs | ✅ |
| Délai avant trigger | 2µs | 2µs | ✅ |
| SETTLE_TIME_US | 200µs | **0µs** | ✅ Corrigé |
| Délai entre mesures | 100ms (variable) | **60ms (fixe)** | ✅ Corrigé |
| MIN_DISTANCE | 0cm | **2cm** | ✅ Corrigé |
| MAX_DISTANCE | 500cm | **400cm** | ✅ Corrigé |
| Timeout | 30ms | 30ms | ✅ |
| Conversion | 58µs/cm | 58µs/cm | ✅ |

---

## 🎯 Conclusion

Le code est maintenant **conforme aux spécifications officielles HC-SR04**.

**Améliorations apportées** :
- ✅ Suppression du délai de stabilisation non documenté
- ✅ Correction du délai entre mesures (valeur fixe)
- ✅ Ajustement des limites de distance selon datasheet
- ✅ Documentation complète des recommandations

**Points d'attention pour l'avenir** :
- Vérifier si le câblage utilise bien un mode 1-pin compatible
- Monitorer la qualité des mesures en production
- Considérer l'ajout de filtrage supplémentaire si nécessaire (Kalman, etc.)

---

*Document généré le : 2025-10-07*
*Basé sur : Datasheet HC-SR04 officielle*

