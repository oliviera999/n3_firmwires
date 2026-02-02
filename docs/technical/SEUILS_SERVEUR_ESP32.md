# Documentation des seuils serveur/ESP32

**Date** : 2026-02-02  
**Version** : 11.190

## Vue d'ensemble

Ce document décrit les différences intentionnelles entre les seuils de validation côté ESP32 et côté serveur PHP. Ces différences sont volontaires et répondent à des besoins différents.

---

## Seuils de validation

### Température eau (TempEau)

| Source | Min | Max | Fichier |
|--------|-----|-----|---------|
| **ESP32** | 5.0°C | 60.0°C | `include/config.h` SensorConfig::WaterTemp (lignes 362-363) |
| **Serveur PHP** | 3.0°C | 50.0°C | `ffp3/src/Service/SensorDataService.php` ligne 36-37 |

**Justification** :
- **ESP32 (5-60°C)** : Plage plus restrictive pour éviter les valeurs aberrantes lors de la mesure. Le capteur DS18B20 peut parfois donner des valeurs erronées lors de problèmes de connexion.
- **Serveur (3-50°C)** : Plage plus large pour le nettoyage de données historiques. Le serveur doit gérer des données qui peuvent avoir été enregistrées avant correction.

**Impact** : Le serveur peut accepter des valeurs que l'ESP32 rejette (ex: 4°C). C'est intentionnel pour permettre le nettoyage de données historiques.

---

### Température air (TempAir)

| Source | Min | Max | Fichier |
|--------|-----|-----|---------|
| **ESP32** | 3.0°C | 50.0°C | `include/config.h` SensorConfig::AirSensor (lignes 372-373) |
| **Serveur PHP** | 3.0°C | - | `ffp3/src/Service/SensorDataService.php` ligne 40 |

**Justification** :
- **ESP32 (3-50°C)** : Plage complète pour validation en temps réel.
- **Serveur (3°C min uniquement)** : Le serveur ne valide que le minimum pour le nettoyage. Pas de maximum car les valeurs élevées sont moins critiques.

**Impact** : Cohérent pour le minimum. Le serveur n'a pas de maximum explicite.

---

### Humidité

| Source | Min | Max | Fichier |
|--------|-----|-----|---------|
| **ESP32** | 10.0% | 100.0% | `include/config.h` SensorConfig::AirSensor (lignes 374-375) |
| **Serveur PHP** | 3.0% | - | `ffp3/src/Service/SensorDataService.php` ligne 43 |

**Justification** :
- **ESP32 (10-100%)** : Plage réaliste pour un capteur DHT22 en environnement intérieur.
- **Serveur (3% min uniquement)** : Minimum très bas pour le nettoyage de données historiques.

**Impact** : Le serveur accepte des valeurs plus basses que l'ESP32 pour le nettoyage.

---

### Eau Aquarium (EauAquarium)

| Source | Min | Max | Fichier |
|--------|-----|-----|---------|
| **ESP32** | 2 cm | 400 cm | `include/config.h` SensorConfig::Ultrasonic (lignes 386-387) |
| **Serveur PHP** | 4.0 cm | 70.0 cm | `ffp3/src/Service/SensorDataService.php` ligne 46-47 |

**Justification** :
- **ESP32 (2-400 cm)** : Plage technique du capteur ultrason HC-SR04.
- **Serveur (4-70 cm)** : Plage réaliste pour un aquarium. Valeurs < 4 cm ou > 70 cm sont considérées comme aberrantes pour le nettoyage.

**Impact** : Le serveur rejette des valeurs que l'ESP32 accepte (ex: 3 cm, 100 cm). C'est intentionnel pour le nettoyage de données.

---

### Eau Réserve (EauReserve)

| Source | Min | Max | Fichier |
|--------|-----|-----|---------|
| **ESP32** | 2 cm | 400 cm | `include/config.h` SensorConfig::Ultrasonic (lignes 386-387) |
| **Serveur PHP** | 10.0 cm | 90.0 cm | `ffp3/src/Service/SensorDataService.php` ligne 50-51 |

**Justification** :
- **ESP32 (2-400 cm)** : Plage technique du capteur ultrason HC-SR04.
- **Serveur (10-90 cm)** : Plage réaliste pour un réservoir. Valeurs < 10 cm ou > 90 cm sont considérées comme aberrantes.

**Impact** : Le serveur rejette des valeurs que l'ESP32 accepte. C'est intentionnel pour le nettoyage de données.

---

## Recommandations

### Pour la synchronisation future

Si une synchronisation complète est souhaitée :

1. **Option 1** : Aligner les seuils serveur sur les seuils ESP32
   - Avantage : Cohérence totale
   - Inconvénient : Perte de capacité de nettoyage de données historiques

2. **Option 2** : Documenter les différences (approche actuelle)
   - Avantage : Flexibilité pour le nettoyage serveur
   - Inconvénient : Risque de confusion

3. **Option 3** : Seuils serveur plus larges que ESP32 (recommandé)
   - Avantage : Le serveur peut nettoyer des données que l'ESP32 aurait rejetées
   - Inconvénient : Nécessite une documentation claire

### Pour le développement

- **ESP32** : Utiliser les seuils stricts pour la validation en temps réel
- **Serveur** : Utiliser les seuils larges pour le nettoyage de données historiques
- **Documentation** : Maintenir ce fichier à jour si les seuils changent

---

## Références

- Configuration ESP32 : `include/config.h`
- Service serveur : `ffp3/src/Service/SensorDataService.php`
- Validation ESP32 : `src/system_sensors.cpp`, `src/app_tasks.cpp`

---

**Note** : Ces différences sont intentionnelles et ne constituent pas un bug. Elles répondent à des besoins différents (validation temps réel vs nettoyage données historiques).
