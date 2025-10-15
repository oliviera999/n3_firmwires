# 📊 RAPPORT DE MONITORING - FFP5CS v11.08
**Date**: 12 octobre 2025, 21:30  
**Environnement**: wroom-test  
**Durée**: ~2 minutes de capture  
**Fichier log**: `monitor_wroom-test_v11.08_2025-10-12_21-30-25.log`

---

## 🎯 RÉSUMÉ EXÉCUTIF

### ✅ Points Positifs
1. **Démarrage système**: OK - Initialisation complète sans crash
2. **WiFi**: Connexion réussie (RSSI: -62 dBm, qualité 74%)
3. **Capteurs**: Tous fonctionnels (eau, air, ultrasoniques)
4. **Serveur Web**: Démarré sur port 80 (12 connexions max)
5. **WebSocket**: Actif sur port 81
6. **OTA**: Système prêt (ArduinoOTA sur port 3232)
7. **Modem Sleep**: Activé automatiquement
8. **Mémoire**: Stable (~110-134 KB heap libre)

### ⚠️ Points d'Attention
1. **DHT (Air Sensor)**: Filtrage avancé échoue, récupération nécessaire (65%)
2. **Servo PWM**: Erreurs de configuration LEDC (pins déjà attachés)
3. **Ultrasonic**: Quelques timeouts sporadiques
4. **OTA**: Espace insuffisant (960 KB < 1 MB requis)
5. **HTTP Stats**: 0 succès / 2684 échecs historiques

### 🔴 Erreurs Critiques Détectées
**AUCUNE** - Pas de Guru Meditation, Panic, Brownout ou Core dump

---

## 📋 ANALYSE DÉTAILLÉE PAR CATÉGORIE

### 1. 🔧 DÉMARRAGE SYSTÈME

#### ✅ Boot Successful
```
rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
Chip type: ESP32-D0WD-V3 (revision v3.1)
Features: Wi-Fi, BT, Dual Core + LP Core, 240MHz
```

**Détails**:
- Reset: POWERON (mise sous tension normale)
- Boot: SPI Fast Flash (optimal)
- Version SDK: v5.5-1-gb66b5448e0
- Fréquence CPU: 240 MHz (2 cores)
- Compteur redémarrages: 16

#### 📦 Filesystem
```
[FS] Mounting LittleFS...
[FS] LittleFS ok: 270336/524288 bytes (51.6% utilisé)
```
**État**: ✅ Monté avec succès

---

### 2. 🌐 CONNECTIVITÉ WiFi

#### Connexion
```
[WiFi] ✅ Connecté à inwi Home 4G 8306D9 (192.168.0.86, RSSI -62 dBm)
```

**Qualité du signal**:
- RSSI initial: -62 dBm (Bon) → -63 dBm après ~13s
- Qualité: 74% (Bon signal)
- Canal: 10
- Hostname: `ffp3-C9EC`
- MAC: EC:C9:FF:E3:59:2C
- Mode: AP+STA (AP SSID: ESP_E3592D, IP: 192.168.4.1)

**Analyse**: Signal stable et bon. Pas de déconnexions détectées.

---

### 3. ⏰ GESTION TEMPORELLE

#### Synchronisation NTP
```
[21:09:01][INFO][NTP] Synchronisation NTP réussie en 0 ms (0 tentatives)
[21:09:01][INFO][DRIFT] Différence temps local vs NTP: 0 secondes (0 ms)
```

**Dérive temporelle**:
- Dérive mesurée: **93.88 PPM** (✅ Sous le seuil de 100 PPM)
- Décalage: 0.34 secondes
- Dernière sync: 21:28:21 (2 minutes avant ce monitoring)
- Statut: ✅ **Normale**

**Analyse**: Excellente stabilité temporelle. Le système de correction de dérive fonctionne bien.

---

### 4. 🔌 CAPTEURS

#### A. DS18B20 (Température Eau)

**Initialisation**:
```
[WaterTemp] Capteur connecté et fonctionnel (test: 28.5°C)
[WaterTemp] Capteur détecté et initialisé (résolution: 10 bits, conversion: 220 ms)
```

**Lectures**:
- Température: 28.5-28.6°C (stable)
- Temps de lecture: 781-1324 ms
- Lissage actif: ✅
- Sauvegarde NVS: ✅

**Problème mineur**:
```
[WaterTemp] Lecture invalide rejetée: -127.0°C (NaN=0, inRange=0)
```
**Impact**: ⚠️ Minime - Une lecture sur plusieurs est invalide, mais le filtrage récupère automatiquement.

#### B. DHT22 (Température/Humidité Air)

**⚠️ PROBLÈME RÉCURRENT**:
```
[AirSensor] Filtrage avancé échoué, tentative de récupération...
[AirSensor] Tentative de récupération 1/2...
[AirSensor] Récupération réussie: 65.0%
```

**Timing**:
- Temps de lecture humidité: 2800-2850 ms (très long)
- Température air: 0 ms (instantané - valeur en cache ?)

**Analyse**: 
- ⚠️ Le DHT nécessite systématiquement une récupération
- Le filtrage avancé échoue à chaque cycle
- La récupération fonctionne (65%) mais ajoute ~2.5s de délai
- **Recommandation**: Revoir la stratégie de lecture DHT ou ajuster les timeouts

#### C. HC-SR04 (Capteurs Ultrasoniques)

**Niveau Aquarium** (GPIO défini):
```
[Ultrasonic] Lecture 1: 9 cm
[Ultrasonic] Lecture 2: 9 cm
[Ultrasonic] Lecture 3: 9 cm
[Ultrasonic] Distance médiane: 9 cm (3 lectures valides)
```
- ✅ Stable, cohérent
- Temps: ~185-214 ms
- Note: Un timeout détecté sur une lecture (récupération OK)

**Niveau Potager**:
```
[Ultrasonic] Distance médiane: 208 cm (3 lectures valides)
```
- ✅ Stable, cohérent
- Temps: ~214-222 ms

**Niveau Réservoir**:
```
[Ultrasonic] Distance médiane: 209 cm (3 lectures valides)
```
- ✅ Stable, cohérent
- Temps: ~219-221 ms
- Une lecture aberrante (179 cm) filtrée par la médiane

**Analyse globale capteurs ultrasoniques**: 
- ✅ Fonctionnement correct avec filtrage médian efficace
- Quelques lectures aberrantes isolées (normales pour HC-SR04)
- Le système de médiane sur 3 lectures filtre bien le bruit

#### Timing Global Capteurs
```
[SystemSensors] ✓ Lecture capteurs terminée en 4287-4766 ms
```
**Répartition**:
- Température eau: 781-1324 ms (29%)
- Humidité: 2800-2850 ms (60%) ⚠️ **Goulot d'étranglement**
- Ultrasoniques: ~640 ms (14%)
- Luminosité: 12-13 ms (<1%)

---

### 5. 🎛️ ACTIONNEURS

#### Servos (GPIO12 et GPIO13)

**⚠️ AVERTISSEMENTS LEDC**:
```
[ERROR] Pin 12 is already attached to LEDC (channel 0, resolution 10)
[ERROR] PWM channel failed to configure on pin 12!
[WARNING] Success to Attach servo : 12 on PWM 0
```

**Analyse**:
- Les servos s'attachent correctement MALGRÉ les erreurs LEDC
- Problème de double-attachement lors de l'initialisation
- **Impact**: ⚠️ Fonctionnel mais logs pollués
- **Recommandation**: Vérifier la séquence d'initialisation des servos pour éviter le double-attachement

#### Pompe (GPIO16)
```
[21:30:37][INFO] Pump GPIO16 ON
```
✅ Démarrage OK

---

### 6. 🖥️ SERVEUR WEB

#### Configuration
```
[Web] AsyncWebServer démarré sur le port 80
[Web] Timeout serveur: 8000 ms
[Web] Connexions max: 12
[Web] WebSocket temps réel sur le port 81
```

#### Activité Détectée
```
[Web] 📊 /json request from 192.168.0.158 - Heap: 134204 bytes
[Web] 📊 /json request from 192.168.0.158 - Heap: 112808 bytes
```

**Analyse**:
- ✅ Client connecté (192.168.0.158) interroge activement l'API
- Mémoire stable pendant les requêtes (112-134 KB)
- Pas de timeouts serveur détectés

---

### 7. 🔄 MISE À JOUR OTA

#### Statut
```
[OTA] ❌ Espace insuffisant: 960.0 KB < 1.0 MB
[OTA] ✅ Aucune mise à jour disponible
[OTA] 📋 Version courante: 11.08
[OTA] 📋 Version distante: (vide)
```

**Détails**:
- Espace libre sketch: **960 KB** (983040 bytes)
- Firmware actuel: 2119632 bytes (~2.02 MB)
- Partition courante: app0 (0x00010000)
- État image: NON DÉFINIE ⚠️

**⚠️ PROBLÈME**: 
- L'espace libre (960 KB) est insuffisant pour une mise à jour OTA complète
- Avec un firmware de 2.02 MB, la partition OTA semble trop petite
- **Recommandation**: Vérifier le schéma de partitions (`partitions_esp32_wroom_test_large.csv`)

---

### 8. 💾 MÉMOIRE

#### Heap
```
Heap libre au démarrage: 130400 bytes
Heap libre minimum historique: 57596 bytes
```

**Évolution pendant l'exécution**:
- Après initialisation: ~170 KB
- Pendant requêtes web: 112-134 KB
- Après envoi mail: ~116 KB
- Heap min actuel: 110700 bytes

**Fragmentation**: 
- Utilisation pic: 130400 - 57596 = **72804 bytes** (historique max)
- Marge actuelle: ~110 KB (confortable)

**Analyse**: 
✅ Utilisation mémoire saine. Pas de fuite détectée sur la période observée.

---

### 9. 📧 EMAIL

#### Test d'Envoi au Démarrage
```
[Mail] ===== DÉTAILS DU MAIL =====
[Mail] Heure d'envoi: 2025-10-12 21:30:46
[Mail] À: Test User <oliv.arn.lau@gmail.com>
[Mail] Objet: (caractères corrompus)
[Mail] Contenu: Test de démarrage FFP3CS v11.08
```

**⚠️ PROBLÈME ENCODAGE**:
```
[Mail] De: ��? <L��?>
[Mail] Objet: ���?0
```
**Analyse**: 
- Le contenu du mail est correct (UTF-8)
- Les métadonnées (De, Objet) semblent corrompues lors de l'affichage série
- **Cause probable**: Problème d'encodage UTF-8 dans le Serial Monitor
- **Impact**: ⚠️ Cosmétique si le mail réel est correct

---

### 10. 🛌 GESTION DU SOMMEIL

#### Modem Sleep
```
[Power] ✅ Modem sleep activé - Réveil WiFi possible
[Power] DTIM configuré, IP: 192.168.0.86
[Power] ✅ Modem sleep activé automatiquement (WiFi connecté)
[Power] Test DTIM récent - skip
```

**Analyse**: 
- ✅ Modem sleep actif (économie d'énergie WiFi)
- Configuration DTIM OK
- Force Wake Up activé (activité web locale)

---

### 11. 📊 DIAGNOSTICS

#### Statistiques Système
```
Compteur de redémarrages: 16
Heap libre minimum historique: 57596 bytes
Requêtes HTTP réussies: 0
Requêtes HTTP échouées: 2684 ⚠️
Mises à jour OTA réussies: 0
Mises à jour OTA échouées: 0
```

**⚠️ ALERTE**: 
- **2684 requêtes HTTP échouées** sans aucun succès !
- Cela indique un problème historique avec la communication serveur distant
- **Recommandation**: Investiguer les endpoints serveur et les timeouts

---

### 12. 🔒 NVS (Stockage Persistant)

#### Variables Enregistrées
```json
{
  "bouffeMatinOk": false,
  "bouffeMidiOk": false,
  "bouffeSoirOk": false,
  "lastJourBouf": -1,
  "pompeAquaLocked": false,
  "forceWakeUp": true,
  "ota.updateFlag": false
}
```

**Analyse**: 
- ✅ État cohérent (nouveau jour - pas de nourrissage)
- Force Wake Up: true (activité web)
- Pompe aquarium: déverrouillée

---

## 🎭 COMPARAISON AVEC RÈGLES DU PROJET

### ✅ Conformité

| Règle | Statut | Commentaire |
|-------|--------|-------------|
| Version incrémentée | ✅ | v11.07 → v11.08 |
| Monitoring 90s minimum | ✅ | ~120 secondes capturées |
| Analyse des logs | ✅ | Rapport complet ci-dessous |
| Pas de Guru Meditation | ✅ | Aucune erreur critique |
| Pas de Panic | ✅ | Aucun panic détecté |
| Pas de Brownout | ✅ | Alimentation stable |
| Watchdog OK | ✅ | Géré par TWDT natif |
| WiFi reconnecté | ✅ | Connexion stable |
| WebSocket actif | ✅ | Port 81 opérationnel |

### ⚠️ Avertissements

| Aspect | Gravité | Description |
|--------|---------|-------------|
| DHT Filtrage | 🟡 Moyenne | Échecs répétés du filtrage avancé, récupération nécessaire |
| Servo LEDC | 🟡 Faible | Double-attachement PWM (fonctionnel mais logs pollués) |
| OTA Espace | 🟡 Moyenne | Espace insuffisant pour mise à jour (960 KB < 1 MB) |
| HTTP Stats | 🔴 Élevée | 0 succès / 2684 échecs historiques |
| Encodage Mail | 🟡 Faible | Métadonnées corrompues dans logs série |

---

## 🔍 POINTS D'ATTENTION PRIORITAIRES

### 1. 🔴 PRIORITÉ 1 - Requêtes HTTP Échouées

**Symptôme**: 
```
Requêtes HTTP réussies: 0
Requêtes HTTP échouées: 2684
```

**Impact**: Communication serveur distant totalement non fonctionnelle historiquement.

**Actions recommandées**:
1. Vérifier la connectivité vers `http://iot.olution.info`
2. Tester manuellement les endpoints:
   - `/ffp3/post-data-test`
   - `/ffp3/api/outputs-test/state`
3. Analyser les timeouts (actuellement 15s)
4. Vérifier les certificats SSL/TLS si HTTPS
5. Activer des logs HTTP détaillés pour diagnostiquer

---

### 2. 🟡 PRIORITÉ 2 - DHT Sensor (Air)

**Symptôme**:
```
[AirSensor] Filtrage avancé échoué, tentative de récupération...
```
(Répété à chaque cycle de 4s)

**Impact**: 
- Augmente le temps de lecture de 2.5s
- Potentiellement des données d'humidité moins fiables

**Actions recommandées**:
1. Augmenter `DHT_MIN_READ_INTERVAL_MS` de 2500 à 3000 ms
2. Ajouter un délai de stabilisation après `dht.begin()`
3. Vérifier la qualité des connexions physiques DHT
4. Tester avec un autre capteur DHT22 si disponible
5. Revoir la logique de "filtrage avancé" (trop stricte ?)

---

### 3. 🟡 PRIORITÉ 3 - Espace OTA Insuffisant

**Symptôme**:
```
[OTA] ❌ Espace insuffisant: 960.0 KB < 1.0 MB
```

**Impact**: Impossible de faire des mises à jour OTA firmware.

**Actions recommandées**:
1. Vérifier le schéma de partitions:
   ```csv
   # partitions_esp32_wroom_test_large.csv
   # Vérifier la taille de app1 (partition OTA)
   ```
2. Envisager:
   - Augmenter la taille de app1 si possible
   - Réduire la taille du firmware (désactiver features non critiques)
   - Utiliser compression du firmware
3. Alternative: Mises à jour via upload série uniquement

---

### 4. 🟡 PRIORITÉ 4 - Servo PWM Warnings

**Symptôme**:
```
[ERROR] Pin 12 is already attached to LEDC (channel 0, resolution 10)
[ERROR] PWM channel failed to configure on pin 12!
```

**Impact**: Fonctionnel mais logs pollués (difficile de détecter vraies erreurs).

**Actions recommandées**:
1. Revoir la séquence d'initialisation dans `actuators.cpp`
2. Vérifier qu'on n'appelle pas `attach()` deux fois
3. Ajouter des vérifications avant `attach()`:
   ```cpp
   if (!servo.attached()) {
       servo.attach(pin, min, max);
   }
   ```

---

## 📈 MÉTRIQUES DE PERFORMANCE

### Temps d'Initialisation
| Module | Temps | Statut |
|--------|-------|--------|
| Boot ESP32 | ~0.8s | ✅ Rapide |
| LittleFS Mount | ~0.1s | ✅ Rapide |
| WiFi Connect | ~4s | ✅ Bon |
| NTP Sync | 0 ms | ✅ Instantané |
| Capteurs Init | ~2s | ✅ Acceptable |
| Web Server | <0.1s | ✅ Instantané |
| **TOTAL** | **~7s** | ✅ **Très bon** |

### Temps de Lecture Capteurs
| Capteur | Temps Moyen | Cible | Statut |
|---------|-------------|-------|--------|
| DS18B20 (Eau) | 1000 ms | <1500 ms | ✅ Bon |
| DHT22 (Air) | 2825 ms | <2500 ms | ⚠️ Légèrement lent |
| HC-SR04 (×3) | 640 ms | <700 ms | ✅ Bon |
| Luminosité | 12 ms | <50 ms | ✅ Excellent |
| **TOTAL** | **4477 ms** | **<5000 ms** | ✅ **Acceptable** |

**Optimisation possible**: Réduire le temps DHT22 de 2.8s à 2.0s (gain 800ms).

---

## 🎯 RECOMMANDATIONS FINALES

### Actions Immédiates (Avant Prochain Déploiement)

1. **🔴 Critique**: Investiguer et corriger le problème HTTP (2684 échecs)
   - Tester manuellement les endpoints
   - Vérifier la connectivité réseau vers le serveur distant
   - Augmenter les logs HTTP pour diagnostic

2. **🟡 Important**: Optimiser le DHT22
   - Augmenter `DHT_MIN_READ_INTERVAL_MS` à 3000 ms
   - Ajouter un délai de stabilisation de 2s après `dht.begin()`
   - Assouplir les critères du "filtrage avancé"

3. **🟡 Important**: Résoudre l'espace OTA
   - Analyser le schéma de partitions
   - Envisager d'augmenter app1 ou réduire le firmware

### Actions Moyen Terme

4. **🟢 Nice-to-have**: Nettoyer les warnings servo
   - Vérifier la logique d'attachement PWM
   - Ajouter des guards pour éviter double-attachement

5. **🟢 Nice-to-have**: Corriger l'encodage UTF-8 des logs email
   - Vérifier la configuration Serial.begin(115200)
   - Potentiellement ajouter `Serial.setDebugOutput(false)`

---

## ✅ CONCLUSION

### Verdict Global: 🟢 **SYSTÈME OPÉRATIONNEL AVEC RÉSERVES**

**Points forts**:
- Démarrage stable sans crash
- Tous les capteurs fonctionnels
- Serveur web et WebSocket actifs
- Mémoire bien gérée
- WiFi stable
- Gestion temporelle excellente

**Points faibles critiques**:
- ❌ Communication serveur HTTP totalement non fonctionnelle (2684 échecs)
- ⚠️ DHT22 nécessite récupération à chaque cycle
- ⚠️ OTA non possible (espace insuffisant)

**Recommandation**: 
1. **Priorité absolue**: Résoudre le problème HTTP avant mise en production
2. Le système est stable localement (capteurs, web, WiFi)
3. Les problèmes DHT et OTA sont gérables mais à corriger

### Prochaines Étapes

1. Créer un ticket pour chaque point d'attention prioritaire
2. Tester la connectivité serveur distant manuellement
3. Déployer une v11.09 avec corrections HTTP + DHT
4. Re-monitorer pendant 10 minutes pour validation

---

**Rapport généré le**: 2025-10-12 21:40  
**Analysé par**: Assistant IA Expert ESP32  
**Version firmware**: FFP3CS v11.08  
**Environnement**: wroom-test (ESP32-WROOM-32)

