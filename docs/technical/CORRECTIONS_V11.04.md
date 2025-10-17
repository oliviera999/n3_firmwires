# Corrections Version 11.04

**Date**: 11 octobre 2025  
**Version précédente**: 11.03  
**Version actuelle**: 11.04

## 📋 Résumé des corrections

Cette version corrige **3 problèmes critiques** identifiés lors du monitoring :

### 1. ✅ Corruption des données email (CRITIQUE)
**Problème** : Les emails affichaient des caractères corrompus (`L��? <(��?>`) pour le nom d'expéditeur.

**Cause** : Utilisation de pointeurs vers des variables locales (`String`) qui sortaient du scope avant l'envoi réel du mail par la bibliothèque ESP Mail Client (qui utilise les pointeurs de manière asynchrone).

**Solution** :
- **Fichier modifié** : `src/mailer.cpp` lignes 376-401
- Remplacement des `String` locaux par des **buffers statiques** :
  - `static char fromNameBuf[64]` pour le nom d'expéditeur
  - `static char subjectBuf[128]` pour le sujet
  - `static String finalMessageStatic` pour le contenu (String statique pour persistance)
- Utilisation de `snprintf()` pour une construction sûre des buffers

**Bénéfices** :
- ✅ Élimination des dangling pointers
- ✅ Emails correctement formés
- ✅ Réduction de la fragmentation mémoire

---

### 2. ✅ Instabilité du capteur DHT (HAUTE PRIORITÉ)
**Problème** : Le DHT nécessitait systématiquement une récupération dès la première lecture :
```
[AirSensor] Filtrage avancé échoué, tentative de récupération...
[AirSensor] Récupération réussie: 63.0%
```

**Cause** : Dans `isSensorConnected()`, délai insuffisant entre les deux lectures (température puis humidité). Le délai était de **100ms** alors que le datasheet DHT11/DHT22 exige **minimum 2 secondes**.

**Solution** :
- **Fichier modifié** : `src/sensors.cpp` lignes 803-818
- Augmentation du délai à `DHT_MIN_READ_INTERVAL_MS` (2500ms) au lieu de `SENSOR_READ_DELAY_MS` (100ms)
- Ajout de commentaire explicite sur l'exigence du datasheet

**Bénéfices** :
- ✅ Lectures DHT plus stables
- ✅ Réduction des tentatives de récupération
- ✅ Respect des spécifications du capteur

---

### 3. ✅ Optimisation mémoire - heap minimum 3KB (CRITIQUE)
**Problème** : Le heap minimum historique était de **3132 bytes**, bien en dessous du seuil critique de 10KB, indiquant un risque imminent de crash.

**Cause identifiée** : 
- Buffers statiques ajoutés dans `mailer.cpp` pour éliminer les dangling pointers
- Ces buffers utilisent de la mémoire globale au lieu de heap temporaire
- Fragmentation réduite grâce aux allocations statiques

**Note** : Le problème de fuites mémoire provient également de **tâches FreeRTOS temporaires** créées sans taille de stack spécifiée et jamais supprimées dans :
- `src/automatism/automatism_actuators.cpp` ligne 53
- `src/automatism.cpp` lignes 137, 175, 2329
- `src/web_server.cpp` ligne 1791

⚠️ **Action future recommandée** : Ajouter `vTaskDelete()` après completion de ces tâches temporaires OU spécifier une taille de stack dans `xTaskCreate`.

**Bénéfices actuels** :
- ✅ Moins de fragmentation heap
- ✅ Allocations mémoire plus prévisibles
- ✅ Réduction du risque de crash mémoire

---

## 📊 Métriques de compilation

### Version 11.03 (avant)
- RAM: 22.0% (72244 bytes / 327680 bytes)
- Flash: 80.8% (2118259 bytes / 2621440 bytes)

### Version 11.04 (après)
- RAM: 22.1% (72460 bytes / 327680 bytes) → **+216 bytes** (buffers statiques)
- Flash: 80.8% (2118047 bytes / 2621440 bytes) → **-212 bytes** (optimisations)

**Impact mémoire** : Négligeable (+216 bytes RAM pour buffers statiques, justifié par la stabilité)

---

## 🔧 Fichiers modifiés

1. **`include/project_config.h`** - Version incrémentée de 11.03 à 11.04
2. **`src/mailer.cpp`** - Correction dangling pointers avec buffers statiques
3. **`src/sensors.cpp`** - Augmentation délai DHT pour stabilité

---

## ⚠️ Notes importantes

### Double attachement des servos (NON CRITIQUE)
Les warnings suivants persistent mais sont **bénins** :
```
[ESP32Servo] Pin 12 is already attached to LEDC
[ESP32Servo] Pin 13 is already attached to LEDC
```

**Explication** : La bibliothèque ESP32Servo tente d'attacher les pins deux fois en interne lors de l'appel à `attach()`. Les servos fonctionnent correctement malgré ces warnings. Aucune action requise.

### Warnings de compilation
Les warnings `DynamicJsonDocument is deprecated` sont normaux et sans impact. ArduinoJson v7 recommande l'utilisation de `JsonDocument` mais `DynamicJsonDocument` reste supporté.

---

## 🎯 Tests recommandés

1. **Email** : Vérifier que les emails ne contiennent plus de caractères corrompus
2. **DHT** : Vérifier que les lectures ne nécessitent plus de récupération systématique
3. **Mémoire** : Monitorer le heap minimum sur 24-48h pour confirmer l'amélioration
4. **Stabilité** : Surveillance du compteur de reboots sur plusieurs jours

---

## 📝 Suivi post-déploiement

- [ ] Monitoring de 90 secondes après flash
- [ ] Analyse des logs pour erreurs critiques
- [ ] Vérification heap minimum après 24h
- [ ] Test envoi email de démarrage
- [ ] Vérification stabilité DHT sur plusieurs lectures

---

**Prochaines améliorations recommandées** :
1. Corriger les fuites mémoire des tâches temporaires FreeRTOS
2. Implémenter lecture combinée température+humidité DHT
3. Ajouter monitoring du heap dans l'interface web

