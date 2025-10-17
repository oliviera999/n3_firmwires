# 📊 Analyse Monitoring ESP32 - 5 minutes
**Date**: 2025-10-11 15:46-15:51  
**Version firmware**: 11.05  
**Durée**: 300 secondes  
**Port**: COM6

---

## 📈 Statistiques globales

| Métrique | Valeur | Notes |
|----------|--------|-------|
| **Lignes analysées** | 3335 | - |
| **Envois détectés** | 292 | ~1 envoi toutes les secondes |
| **Erreurs critiques** | 18 | ⚠️ PROBLÈMES SERVEUR |
| **Warnings** | 16 | Capteur DHT22 instable |
| **Heap libre moyen** | ~75 KB | Stable autour de 74-75 KB |
| **Heap minimal** | 3132 bytes | ⚠️ Faible mais constant |
| **Reboots** | 4197 | Nombre cumulé depuis origin |

---

## 🔴 PROBLÈMES CRITIQUES IDENTIFIÉS

### 1. ❌ Heartbeat.php introuvable (404)
**Fréquence**: Toutes les ~33 secondes  
**Impact**: ÉLEVÉ - Système de monitoring du serveur non fonctionnel

#### Détails
```
[HTTP] → http://iot.olution.info/ffp3/ffp3datas/heartbeat.php (56 bytes)
[HTTP] payload: uptime=190&free=75132&min=3132&reboots=4197&crc=129BAD61
[HTTP] ← code 404, 2067 bytes
[HTTP] response: <!doctype html>...404 Not Found...
```

#### Occurrences dans les 5 minutes
- 15:46:22 - uptime=190s
- 15:46:56 - uptime=224s
- 15:47:29 - uptime=257s
- 15:48:02 - uptime=290s
- 15:48:37 - uptime=325s
- 15:49:11 - uptime=359s
- 15:49:45 - uptime=393s
- 15:50:19 - uptime=427s
- 15:50:53 - uptime=460s

**Total**: 9 tentatives, **9 échecs (100%)**

#### 🔧 Causes possibles
1. **Fichier déplacé ou supprimé** sur le serveur distant
2. **Changement de structure de répertoire** (`ffp3/ffp3datas/` vs autre chemin)
3. **Permissions serveur** - fichier PHP non accessible
4. **Configuration serveur** - routing incorrect

#### ✅ Solution recommandée
```cpp
// Vérifier l'URL exacte sur le serveur
// Option 1: Corriger l'URL dans le code
#define HEARTBEAT_URL "http://iot.olution.info/ffp3/heartbeat.php"  // Sans /ffp3datas/

// Option 2: Créer le fichier manquant sur le serveur
// Emplacement: /ffp3/ffp3datas/heartbeat.php
```

---

### 2. ❌ Post-data-test retourne 500 (Erreur serveur)
**Fréquence**: Chaque envoi de données (toutes les 5 secondes)  
**Impact**: CRITIQUE - Les données sont partiellement enregistrées mais avec erreur

#### Détails
```
[HTTP] → http://iot.olution.info/ffp3/post-data-test (486 bytes)
[HTTP] payload: api_key=fdGTMoptd5CD2ert3&sensor=esp32-wroom&version=11.05&TempAir=28.2&Humidite=63.0&TempEau=27.8...
[HTTP] ← code 500, 104 bytes
[HTTP] response: Données enregistrées avec succèsUne erreur serveur est survenue. Veuillez réessayer ultérieurement.
```

#### Occurrences dans les 5 minutes
- 15:47:15 - 3 réponses 500 consécutives
- 15:49:16 - 3 réponses 500 consécutives

**Total**: 2 envois de données, **6 réponses d'erreur** (retries inclus)

#### 🤔 Analyse du message d'erreur
Le serveur retourne un message contradictoire:
- ✅ `"Données enregistrées avec succès"`
- ❌ `"Une erreur serveur est survenue"`

**→ Cela indique probablement:**
1. L'insertion des données dans la BDD réussit
2. Une opération secondaire échoue (log, notification, etc.)
3. Le serveur retourne quand même HTTP 500 au lieu de 200

#### 🔧 Actions requises

**Côté serveur (PRIORITAIRE):**
```php
// Dans post-data-test.php
// Vérifier les logs PHP/MySQL pour identifier l'erreur exacte
// Séparer les opérations critiques (INSERT) des secondaires (logs)
// Retourner HTTP 200 si l'insertion principale réussit
```

**Côté ESP32 (OPTIONNEL):**
```cpp
// Accepter le code 500 si le message contient "succès"
if (httpCode == 500 && response.indexOf("succès") > 0) {
  Serial.println("[HTTP] ⚠️ Données envoyées malgré erreur serveur");
  // Considérer comme succès partiel
}
```

---

## 🟡 POINTS D'ATTENTION

### 3. ⚠️ Capteur DHT22 (Humidité/Température air) instable
**Fréquence**: Environ 1 échec toutes les 10 secondes  
**Impact**: MOYEN - Récupération automatique fonctionne, mais ralentit les lectures

#### Statistiques
- **Échecs de filtrage avancé**: ~60 occurrences en 5 minutes
- **Récupérations réussies**: ~90% (après 1-2 tentatives)
- **Échecs totaux (NaN)**: 6 occurrences (10%)
- **Temps de récupération**: 2.5-3.8 secondes

#### Exemples
```
[15:46:14.271] [AirSensor] Filtrage avancé échoué, tentative de récupération...
[15:46:16.784] [AirSensor] Tentative de récupération 1/2...
[15:46:17.086] [AirSensor] Récupération réussie: 63.0%
[15:46:17.086] [SystemSensors] ⏱️ Humidité: 2829 ms
```

#### Valeurs lues (quand succès)
- Humidité: 63-95% (variations importantes, possiblement réelles)
- Température air: 28-30°C (cohérent)

#### 🔧 Recommandations
1. **Hardware**: Vérifier le câblage du DHT22 (pull-up 10kΩ?)
2. **Logiciel**: Augmenter le délai entre lectures (actuellement ~5s)
3. **Environnement**: Possibles interférences électromagnétiques
4. **Capteur**: Envisager remplacement si défaillance persiste

```cpp
// Dans sensors.cpp - Augmenter le délai minimal
#define DHT_MIN_INTERVAL 3000  // Augmenter à 3s au lieu de 2s
```

---

### 4. 📊 Système de marée fonctionne correctement
**Fréquence**: Calcul toutes les ~1 seconde  
**Impact**: Positif - Détection fonctionnelle

#### Observations
```
[Maree] Calcul15s: actuel=209, diff15s=0 cm
[Maree] Calcul15s: actuel=208, diff15s=1 cm (variation détectée)
[Auto] Marée (10s): wlAqua=209 cm, diff10s=0 cm, dir=0
```

- **Niveau aquarium**: Stable entre 208-209 cm
- **Variations détectées**: ±1 cm (normales)
- **Direction marée**: 0 (stable, pas de montée/descente significative)
- **Pas de déclenchement de mise en veille**: Normal, pas de marée montante

---

### 5. 🌡️ Capteurs de température eau (DS18B20) excellents
**Fréquence**: Lecture toutes les 5 secondes  
**Impact**: Positif - Très stable

#### Performance
- **Temps de lecture**: 773-884 ms (résolution 10 bits)
- **Stabilité**: ±0.2°C sur 5 minutes
- **Valeurs**: 27.8-28.2°C (cohérentes)
- **Taux de succès**: 100% (aucun échec)

```
[WaterTemp] Température filtrée: 28.0°C (médiane: 28.0°C, lissée: 28.0°C, 2 lectures, résolution: 10 bits)
[SystemSensors] ⏱️ Température eau: 780 ms
```

✅ **Aucun problème identifié**

---

### 6. 📡 Capteurs ultrason (HC-SR04) excellents
**Fréquence**: 3 lectures × 3 capteurs toutes les 5 secondes  
**Impact**: Positif - Très fiables

#### Performance par capteur
| Capteur | Valeur moyenne | Stabilité | Temps lecture |
|---------|---------------|-----------|---------------|
| **Potager** | 209 cm | ±0 cm | 219-220 ms |
| **Aquarium** | 208-209 cm | ±1 cm | 219-220 ms |
| **Réservoir** | 208 cm | ±0 cm | 219-224 ms |

```
[Ultrasonic] Lecture 1: 209 cm
[Ultrasonic] Lecture 2: 209 cm
[Ultrasonic] Lecture 3: 209 cm
[Ultrasonic] Distance médiane: 209 cm (3 lectures valides)
[SystemSensors] ⏱️ Niveau potager: 219 ms
```

✅ **Aucun problème identifié**

---

### 7. 🔄 Cycle de lecture complet des capteurs
**Fréquence**: Toutes les ~5 secondes  
**Durée totale**: 4.3-5.3 secondes

#### Décomposition du temps
| Capteur/Opération | Temps moyen | % du total |
|-------------------|-------------|-----------|
| Température eau (DS18B20) | 780 ms | 15% |
| Température air (DHT22) | 25-106 ms | 2-5% |
| **Humidité (DHT22)** | **2.8-3.8 s** | **60-70%** ⚠️ |
| Niveau potager (HC-SR04) | 220 ms | 4% |
| Niveau aquarium (HC-SR04) | 220 ms | 4% |
| Niveau réservoir (HC-SR04) | 220 ms | 4% |
| Luminosité | 13 ms | <1% |
| **TOTAL** | **4.3-5.3 s** | **100%** |

⚠️ **Le DHT22 est le goulot d'étranglement** (60-70% du temps de lecture)

---

### 8. 📶 WiFi stable
**Fréquence**: Vérification toutes les 30 secondes  
**Impact**: Positif

```
[15:46:30.343] [WiFi] Vérification stabilité - RSSI: -66 dBm (Acceptable)
[15:49:15.340] [WiFi] Vérification stabilité - RSSI: -66 dBm (Acceptable)
[15:50:56.779] [WiFi] Vérification stabilité - RSSI: -66 dBm (Acceptable)
```

- **Signal**: -66 dBm (acceptable, pas excellent)
- **Stabilité**: Aucune déconnexion pendant 5 minutes
- **Reconnexion**: Non testée (pas de déconnexion)

✅ **Fonctionnement normal**

---

### 9. 💾 Mémoire stable mais heap minimal faible
**Fréquence**: Continue  
**Impact**: Moyen - À surveiller

#### Statistiques mémoire
| Métrique | Valeur | Seuil | Status |
|----------|--------|-------|--------|
| **Heap libre** | 74-75 KB | >50 KB | ✅ OK |
| **Heap minimal** | 3132 bytes | >5 KB | ⚠️ ATTENTION |
| **Fragmentation** | Non mesurée | - | ❓ |

```
[HTTP] payload: uptime=190&free=75132&min=3132&reboots=4197&crc=129BAD61
[HTTP] payload: uptime=460&free=74720&min=3132&reboots=4197&crc=96C29122
```

#### 🔍 Analyse
- **Heap actuel stable**: Pas de fuite mémoire visible sur 5 minutes
- **Heap minimal très bas**: 3132 bytes = **3.1 KB seulement**
  - Risque de crash si pic d'allocation
  - Possiblement atteint lors d'un pic passé, pas pendant ce monitoring

#### 🔧 Actions recommandées
1. **Monitoring prolongé** pour identifier le pic
2. **Optimiser les buffers** des requêtes HTTP si nécessaire
3. **Ajouter des logs** pour tracer les allocations importantes

---

### 10. 🎛️ Actionneurs stables
**Fréquence**: Vérification toutes les 5 secondes  
**Impact**: Positif

#### État des relais/GPIO
| Actionneur | État | Commandes redondantes |
|------------|------|----------------------|
| **Pompe aquarium** (GPIO 16) | ON | ~60/5min |
| **Pompe réservoir** (GPIO 18) | OFF (verrouillée) | ~60/5min |
| **Chauffage** (GPIO 2) | ON | ~60/5min |
| **Lumière UV** (GPIO 15) | ON | ~60/5min |

```
[Auto] Pompe aqua GPIO commande IGNORÉE - état déjà ON (commande redondante)
[Auto] Arrêt manuel pompe réservoir IGNORÉ - pompe verrouillée par sécurité
[Auto] Chauffage GPIO commande IGNORÉE - état déjà ON (commande redondante)
[Auto] Lumière GPIO commande IGNORÉE - état déjà ON (commande redondante)
```

✅ **Fonctionnement normal** - Les commandes redondantes sont correctement ignorées

---

### 11. 🌐 Communication avec serveur distant
**Fréquence**: Toutes les 5 secondes  
**Impact**: Fonctionnel mais avec erreurs

#### Flux de communication
```
Toutes les 5s:
  1. Lecture capteurs (4-5s)
  2. GET remote state → HTTP 200 (237 bytes) ✅
  3. Parse JSON ✅
  4. Application des commandes GPIO ✅
  5. POST data (toutes les 60s) → HTTP 500 ⚠️
  6. Heartbeat (toutes les 33s) → HTTP 404 ❌
```

#### GET remote state (✅ Fonctionne)
```
[HTTP] → GET remote state
[Web] GET remote state -> HTTP 200
[HTTP] ← received 237 bytes
[Web] ✓ Remote JSON parsed successfully
[Config] Variables distantes inchangées - pas de sauvegarde NVS
```

**Taux de succès**: 100% (~60 requêtes en 5 minutes)

---

## 🔧 ACTIONS REQUISES PAR PRIORITÉ

### 🔴 PRIORITÉ 1 - CRITIQUE (À faire IMMÉDIATEMENT)

#### 1.1 Corriger heartbeat.php (404)
**Responsable**: Admin serveur  
**Temps estimé**: 5 minutes

**Actions**:
```bash
# Sur le serveur iot.olution.info
cd /path/to/ffp3/ffp3datas/
ls -la heartbeat.php  # Vérifier si existe

# Si absent, créer ou déplacer le fichier
# Si présent, vérifier les permissions
chmod 644 heartbeat.php
```

**Alternative côté ESP32**:
```cpp
// Dans mailer.cpp ou équivalent
#define HEARTBEAT_URL "http://iot.olution.info/ffp3/heartbeat.php"
// Supprimer /ffp3datas/ si le fichier est à la racine de ffp3/
```

#### 1.2 Corriger l'erreur 500 de post-data-test
**Responsable**: Développeur backend  
**Temps estimé**: 15-30 minutes

**Actions**:
```php
// Dans post-data-test.php sur le serveur

// 1. Activer les logs d'erreurs
error_reporting(E_ALL);
ini_set('display_errors', 1);
ini_set('log_errors', 1);
ini_set('error_log', '/path/to/php_errors.log');

// 2. Analyser les logs MySQL/PHP
// Identifier quelle opération cause l'erreur après l'INSERT

// 3. Séparer les opérations
try {
    // INSERT principal (CRITIQUE)
    $result = mysqli_query($conn, $insert_query);
    
    if ($result) {
        // Opérations secondaires (NON CRITIQUES)
        try {
            // Log, notifications, etc.
        } catch (Exception $e) {
            // Logger mais ne pas fail la requête
            error_log("Secondary operation failed: " . $e->getMessage());
        }
        
        // Retourner succès si INSERT principal OK
        http_response_code(200);
        echo "Données enregistrées avec succès";
    } else {
        http_response_code(500);
        echo "Erreur: " . mysqli_error($conn);
    }
} catch (Exception $e) {
    http_response_code(500);
    echo "Erreur serveur: " . $e->getMessage();
}
```

---

### 🟡 PRIORITÉ 2 - IMPORTANT (Cette semaine)

#### 2.1 Stabiliser le capteur DHT22
**Responsable**: Technicien hardware/développeur firmware  
**Temps estimé**: 1-2 heures

**Actions**:
1. **Vérifier le hardware**:
   - Résistance pull-up 10kΩ sur la ligne DATA
   - Câble pas trop long (<50cm recommandé)
   - Alimentation 3.3V stable
   
2. **Optimiser le firmware**:
```cpp
// Dans sensors.cpp
#define DHT_MIN_INTERVAL 3000  // Augmenter à 3s
#define DHT_MAX_RETRIES 3      // Limite de tentatives
#define DHT_TIMEOUT 5000       // Timeout plus long

// Ajouter un délai après échec
if (!dht_read_success) {
    vTaskDelay(pdMS_TO_TICKS(500));  // Pause de 500ms avant retry
}
```

3. **Envisager un remplacement** si problème persiste

#### 2.2 Investiguer le heap minimal de 3.1 KB
**Responsable**: Développeur firmware  
**Temps estimé**: 2-3 heures

**Actions**:
```cpp
// Ajouter du monitoring détaillé
void logHeapBeforeOperation(const char* operation) {
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t min_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    Serial.printf("[HEAP] %s - Free: %d, Min: %d\n", operation, free_heap, min_heap);
}

// Utiliser avant chaque opération importante
logHeapBeforeOperation("HTTP POST");
logHeapBeforeOperation("JSON Parse");
logHeapBeforeOperation("Sensor Read");
```

**Objectif**: Identifier quel code cause le pic d'allocation

---

### 🟢 PRIORITÉ 3 - AMÉLIORATION (Quand possible)

#### 3.1 Optimiser le temps de lecture des capteurs
**Gain potentiel**: -40% du temps total  
**Complexité**: Moyenne

**Actions**:
```cpp
// Paralléliser les lectures de capteurs indépendants
// Utiliser des tâches FreeRTOS

void readSensorsParallel() {
    // Lancer DS18B20 en premier (780ms)
    waterTempSensor.requestTemperatures();
    
    // Pendant que DS18B20 mesure, lire DHT22
    dht.readHumidity();  // ~2.8s
    
    // Récupérer DS18B20 (temps déjà écoulé)
    float temp = waterTempSensor.getTempCByIndex(0);
}

// Gain: ~500-800ms par cycle
```

#### 3.2 Améliorer la qualité du signal WiFi
**RSSI actuel**: -66 dBm (acceptable)  
**Cible**: -55 dBm ou mieux

**Actions**:
- Rapprocher l'ESP32 du routeur
- Utiliser une antenne externe si possible
- Vérifier les interférences (autres WiFi, Bluetooth, micro-ondes)

---

## 📋 RÉCAPITULATIF - CHECKLIST

### ✅ Points positifs
- [x] Capteurs ultrason (HC-SR04): Excellents
- [x] Température eau (DS18B20): Très stable
- [x] WiFi: Stable, pas de déconnexion
- [x] Mémoire heap: Pas de fuite visible
- [x] Actionneurs: Fonctionnels
- [x] GET remote state: 100% succès
- [x] Système de marée: Détection fonctionnelle
- [x] Watchdog: Aucun timeout

### ❌ Points à corriger
- [ ] **URGENT**: heartbeat.php retourne 404 (100% échec)
- [ ] **URGENT**: post-data-test retourne 500 (message contradictoire)
- [ ] DHT22 instable (10% échecs complets, 60% retries)
- [ ] Heap minimal très bas (3.1 KB)
- [ ] Temps de lecture capteurs long (4.5s moyenne)

### ⚠️ Points à surveiller
- [ ] Humidité DHT22: Variations 63-95% (réelles ou artefacts?)
- [ ] Commandes GPIO redondantes fréquentes (normal?)
- [ ] Impact des erreurs 500 sur la base de données
- [ ] Fragmentation mémoire (non mesurée)

---

## 🎯 CONCLUSION

### Stabilité générale: 🟡 ACCEPTABLE avec réserves

**Points forts**:
- ✅ Système embarqué stable (pas de crash/reboot)
- ✅ Capteurs critiques (eau, niveaux) excellents
- ✅ Communication WiFi fiable
- ✅ Logique métier fonctionnelle

**Points critiques**:
- ❌ Heartbeat serveur non fonctionnel → Monitoring impossible
- ⚠️ Erreurs serveur 500 malgré succès partiel
- ⚠️ DHT22 instable → Ralentit tout le système

### Recommandation finale

**DÉPLOIEMENT**: ⚠️ Conditionnel

Le système est fonctionnel pour ses missions principales (mesure, contrôle), MAIS:
1. **Corriger heartbeat.php avant déploiement production**
2. **Investiguer l'erreur 500** pour éviter corruption BDD
3. **Envisager remplacement DHT22** si problème persiste

**Prochaine étape**: 
- Monitoring prolongé (30-60 min) après correction des erreurs serveur
- Analyser les logs serveur PHP/MySQL pour l'erreur 500

---

**Rapport généré le**: 2025-10-11 15:51  
**Durée d'analyse**: 5 minutes de monitoring + 10 minutes d'analyse  
**Fichier source**: `monitor_data_send_5min_2025-10-11_15-46-11.log`

