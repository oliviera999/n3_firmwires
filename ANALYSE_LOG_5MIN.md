# Analyse des Logs ESP32 - Surveillance 5 minutes

**Date:** 23/09/2025 19:09-19:14  
**Durée:** 5 minutes  
**Lignes capturées:** 2021  

## 📊 Résumé Exécutif

Le système ESP32 fonctionne de manière stable avec quelques points d'attention :

### ✅ Points Positifs
- **Communication HTTP stable** : Toutes les requêtes vers le serveur distant réussissent (HTTP 200)
- **Système de marée fonctionnel** : Calculs de niveau d'eau cohérents (170-171 cm)
- **Pompe aqua activée** : GPIO 16 maintenu à ON pendant toute la période
- **Heartbeat régulier** : Envoi de données de santé toutes les ~40 secondes
- **Pas d'erreurs critiques** : Aucun crash ou reset détecté

### ⚠️ Points d'Attention

#### 1. Capteur DHT (Air) - Problème Majeur
```
[AirSensor] Capteur DHT non détecté ou déconnecté
[AirSensor] Échec de toutes les tentatives de récupération
[SystemSensors] Température air invalide finale: nan°C, force NaN
[SystemSensors] Humidité invalide finale: nan%, force NaN
```
- **Impact:** Capteur d'air complètement défaillant
- **Action:** Vérifier la connexion physique du capteur DHT

#### 2. Capteur Ultrasonique - Instabilité
```
[Ultrasonic] Saut détecté: 170 cm -> 209 cm (écart: 39 cm)
[Ultrasonic] Saut vers le haut, utilise ancienne valeur par sécurité
[Ultrasonic] Lecture 1 timeout
```
- **Impact:** Lectures intermittentes avec timeouts
- **Action:** Vérifier l'alimentation et la connexion du capteur ultrasonique

#### 3. Capteur Température Eau - Filtrage Strict
```
[WaterTemp] Changement temporel trop important rejeté: 33.3°C -> 28.5°C (écart: 4.8°C)
[WaterTemp] Pas assez de lectures valides (0/4), retourne NaN
```
- **Impact:** Filtrage trop strict rejette des valeurs valides
- **Action:** Ajuster les seuils de filtrage temporel

## 🔄 Comportements Système Observés

### Communication Distante
- **Fréquence:** Requêtes GET remote state toutes les ~10 secondes
- **Réponse:** 243 bytes, JSON valide
- **Commandes GPIO reçues:**
  - GPIO 16 (Pompe aqua): ON (maintenu)
  - GPIO 18 (Pompe réservoir): OFF (maintenu)
  - GPIO 13 (Radiateurs): ON (maintenu)
  - GPIO 15 (Lumière): OFF (maintenu)

### Gestion de la Marée
- **Niveau stable:** 170-171 cm
- **Calculs diff10s:** Principalement 0 cm (niveau stable)
- **Sleep adaptatif:** Délai réduit à cause des erreurs récentes

### Envoi de Données
- **Heartbeat:** Toutes les ~40 secondes vers `heartbeat.php`
- **Données complètes:** Envoi vers `post-ffp3-data2.php` avec 499 bytes
- **Payload exemple:** `uptime=340&free=86928&min=86896&reboots=3&crc=0380C836`

## 📈 Métriques de Performance

### Mémoire
- **Free heap:** ~86928 bytes (stable)
- **Min heap:** 86896 bytes
- **Reboots:** 3 depuis le démarrage

### WiFi
- **RSSI:** -73 à -82 dBm (signal faible à très faible)
- **Stabilité:** Connexion maintenue malgré le signal faible

### Temps de Fonctionnement
- **Uptime:** 340 secondes (5min40s) à la fin de la capture
- **Sauvegarde Power:** Effectuée à 19:13:23

## 🎯 Recommandations Prioritaires

### 1. Urgent - Capteur DHT
```bash
# Vérifier la connexion physique
# GPIO du capteur DHT dans pins.h
# Tester avec un nouveau capteur DHT22
```

### 2. Important - Capteur Ultrasonique
```bash
# Vérifier l'alimentation 5V
# Contrôler les connexions TRIG/ECHO
# Ajuster les timeouts si nécessaire
```

### 3. Moyen - Filtrage Température
```cpp
// Dans sensors.cpp, ajuster les seuils :
// TEMP_MAX_CHANGE_PER_SECOND de 4.8°C à 6.0°C
```

### 4. Optionnel - Signal WiFi
```bash
# Considérer un répéteur WiFi
# Ou ajuster la position de l'antenne
```

## 📋 Actions Immédiates

1. **Vérifier le capteur DHT** - Problème le plus critique
2. **Tester le capteur ultrasonique** - Instabilité des lectures
3. **Monitorer la température eau** - Vérifier si les valeurs rejetées sont valides
4. **Améliorer le signal WiFi** - RSSI trop faible

## 🔍 Points de Surveillance Continue

- **Stabilité des communications HTTP**
- **Cohérence des niveaux d'eau**
- **Fonctionnement des pompes**
- **Intégrité des données envoyées**

---
*Analyse générée automatiquement le 23/09/2025 à 19:14*
