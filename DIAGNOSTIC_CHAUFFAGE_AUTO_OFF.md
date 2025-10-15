# 🔥 Diagnostic - Chauffage S'Arrête Automatiquement

**Date**: 14 Octobre 2025  
**Version**: 11.35  
**Problème**: Chauffage s'éteint quelques secondes après activation locale  

---

## 🚨 Problème Identifié

### Symptômes
```
T+0s:  Utilisateur active chauffage depuis interface web locale
       → GPIO ON immédiatement ✓
       → Feedback UI immédiat ✓
       
T+5-10s: Chauffage s'éteint tout seul ❌
         → Utilisateur frustré
         → État incohérent
```

---

## 🔍 Cause Racine (Hypothèse)

### Séquence Problématique

```
1. Utilisateur active chauffage local
   └─> POST vers serveur: etatHeat=1
   └─> [HTTP] → http://iot.olution.info/ffp3/post-data-test
   └─> [HTTP] payload: ...&etatHeat=1&...
   └─> [HTTP] ← code 200 "Données enregistrées avec succès"

2. Serveur distant reçoit le POST
   ⚠️ MAIS: Ne met PAS À JOUR l'état "heat" dans sa BDD !
   └─> Possibilité 1: Le serveur ignore le champ "etatHeat"
   └─> Possibilité 2: Le serveur met à jour une autre table
   └─> Possibilité 3: Bug serveur PHP (rollback transaction)

3. ESP32 fait GET remote state (toutes les 5-10s)
   └─> [HTTP] → GET remote state
   └─> Serveur retourne l'ANCIEN état: "heat": "0"
   └─> [Network] === JSON REÇU DU SERVEUR ===
       {
         "2": "0",
         "heat": "0",    ← ❌ Toujours OFF côté serveur !
         ...
       }

4. Priorité locale expire (5 secondes)
   └─> hasRecentLocalAction(5000) → false
   └─> hasPendingSync() → false (si POST réussi)
   └─> handleRemoteActuators() s'exécute

5. ESP32 applique l'état distant "heat=0"
   └─> if (doc.containsKey("heat")) {
           auto v = doc["heat"];  // v = "0"
           if (isFalse(v)) {
               autoCtrl.stopHeaterManualLocal();  ← ❌ ARRÊT !
               Serial.println(F("[Network] Chauffage OFF (état distant)"));
           }
       }
   └─> [Network] Chauffage OFF (état distant)
   └─> [Actuators] 🧊 Arrêt manuel chauffage (local)...
   └─> [INFO] Heater GPIO2 OFF
```

---

## 🎯 Root Cause

### Serveur Distant Ne Persiste PAS l'État Actionneurs

Le serveur PHP (`ffp3/post-data-test`) fait probablement :
```php
// Reçoit POST avec etatHeat=1
$data = $_POST;

// Enregistre dans table principale (ffp3Data2)
INSERT INTO ffp3Data2 (..., etatHeat, ...) VALUES (..., $data['etatHeat'], ...);

// ⚠️ MAIS N'ENREGISTRE PAS dans table d'état (outputs ou similar)
// Donc le GET /api/outputs-test/state retourne toujours l'ancien état !
```

**Preuve dans logs monitoring**:
```
POST envoyé: etatHeat=1
GET reçu ensuite: "heat": "0"  ← ❌ Pas mis à jour !
```

---

## ✅ Solutions Possibles

### Solution 1: **Serveur Distant - Persister États** ⭐ RECOMMANDÉ

**Modifier le serveur PHP** pour persister les états actionneurs :

```php
// Fichier: ffp3/post-data-test (ou endpoint correspondant)

// 1. Enregistrer dans table historique (existant)
$sql = "INSERT INTO ffp3Data2 (...) VALUES (...)";
mysqli_query($conn, $sql);

// 2. AJOUTER: Mettre à jour table d'état (NOUVEAU)
if (isset($_POST['etatHeat'])) {
    $heat_state = intval($_POST['etatHeat']);
    
    // Upsert dans table outputs
    $sql = "INSERT INTO outputs (sensor, heat, updated_at) 
            VALUES (?, ?, NOW())
            ON DUPLICATE KEY UPDATE 
            heat = VALUES(heat),
            updated_at = NOW()";
    
    $stmt = $conn->prepare($sql);
    $stmt->bind_param("si", $_POST['sensor'], $heat_state);
    $stmt->execute();
}

// Faire de même pour pump_aqua, pump_tank, light
```

### Solution 2: **ESP32 - Ignorer État Distant Si Action Locale Récente**

**Modifier la logique de priorité locale** :

```cpp
// Fichier: src/automatism/automatism_network.cpp, lignes 568-577

// AVANT (v11.35):
else if (doc.containsKey("heat")) {
    auto v = doc["heat"];
    if (isTrue(v)) {
        autoCtrl.startHeaterManualLocal();
        Serial.println(F("[Network] Chauffage ON (état distant)"));
    } else if (isFalse(v)) {
        autoCtrl.stopHeaterManualLocal();  // ❌ Problème ici
        Serial.println(F("[Network] Chauffage OFF (état distant)"));
    }
}

// APRÈS (v11.36 - PROPOSITION):
else if (doc.containsKey("heat")) {
    auto v = doc["heat"];
    
    // Vérifier si état distant diffère de l'état local NVS
    bool localState = AutomatismPersistence::loadCurrentActuatorState("heater", false);
    bool remoteState = isTrue(v);
    
    if (localState != remoteState) {
        // Vérifier si action locale TRÈS récente (étendre délai)
        if (AutomatismPersistence::hasRecentLocalAction(30000)) { // 30s au lieu de 5s
            uint32_t elapsed = millis() - AutomatismPersistence::getLastLocalActionTime();
            Serial.printf("[Network] ⚠️ État distant (%s) diffère du local (%s), "
                         "mais action locale récente (%lu ms) - IGNORE distant\n",
                         remoteState ? "ON" : "OFF",
                         localState ? "ON" : "OFF",
                         elapsed);
            return; // Ignorer l'état distant
        }
    }
    
    // Sinon, appliquer normalement
    if (isTrue(v)) {
        autoCtrl.startHeaterManualLocal();
        Serial.println(F("[Network] Chauffage ON (état distant)"));
    } else if (isFalse(v)) {
        autoCtrl.stopHeaterManualLocal();
        Serial.println(F("[Network] Chauffage OFF (état distant)"));
    }
}
```

### Solution 3: **ESP32 - Vérifier Cohérence Avant Application** 🛡️

**Ajouter validation croisée** :

```cpp
// Nouvelle fonction dans automatism_network.cpp

bool AutomatismNetwork::shouldApplyRemoteState(
    const char* actuator, 
    bool remoteState,
    bool currentLocalState
) {
    // 1. Si états identiques, OK
    if (remoteState == currentLocalState) {
        return true;
    }
    
    // 2. Si action locale récente (30s), IGNORER distant
    if (AutomatismPersistence::hasRecentLocalAction(30000)) {
        Serial.printf("[Network] ⚠️ Action locale récente, ignore état distant %s\n", actuator);
        return false;
    }
    
    // 3. Si dernière sync locale non confirmée par serveur
    bool localNvsState = AutomatismPersistence::loadCurrentActuatorState(actuator, false);
    if (localNvsState != remoteState) {
        Serial.printf("[Network] ⚠️ État NVS local (%s) diffère du distant (%s) pour %s\n",
                     localNvsState ? "ON" : "OFF",
                     remoteState ? "ON" : "OFF",
                     actuator);
        
        // Logger pour diagnostic
        Serial.println("[Network] 🔍 CONFLIT DÉTECTÉ - Besoin investigation serveur");
        return false; // Par sécurité, garder état local
    }
    
    return true;
}
```

### Solution 4: **ESP32 - Persistance Serveur Forcée** 📤

**Modifier le POST pour forcer mise à jour** :

```cpp
// Ajouter un flag spécial au payload
if (extraPairs && strstr(extraPairs, "etatHeat=") != nullptr) {
    payload += "&forceUpdate=1";  // Nouveau flag
    payload += "&actuatorChanged=heater";  // Indiquer quel actionneur
}
```

Serveur PHP détecte `forceUpdate=1` et fait l'upsert dans table outputs.

---

## 🔧 Solution Recommandée (v11.36)

### Approche Hybride : ESP32 + Serveur

#### Côté ESP32 : Étendre Priorité Locale Conditionnelle

```cpp
// Si POST réussi mais état distant incohérent après,
// étendre la priorité locale jusqu'à confirmation serveur

bool AutomatismNetwork::isRemoteStateTrusted(const char* actuator, bool remoteState) {
    // Charger état NVS local
    bool localState = AutomatismPersistence::loadCurrentActuatorState(actuator, false);
    
    // Si différents ET action locale récente (30s)
    if (localState != remoteState && 
        AutomatismPersistence::hasRecentLocalAction(30000)) {
        return false; // État distant non fiable
    }
    
    return true;
}

// Utiliser dans handleRemoteActuators():
if (doc.containsKey("heat")) {
    auto v = doc["heat"];
    bool remoteState = isTrue(v);
    
    if (!isRemoteStateTrusted("heater", remoteState)) {
        Serial.printf("[Network] ⚠️ État distant chauffage non fiable, conserve local\n");
        return;
    }
    
    // Appliquer normalement
    if (remoteState) {
        autoCtrl.startHeaterManualLocal();
    } else {
        autoCtrl.stopHeaterManualLocal();
    }
}
```

#### Côté Serveur : Persister États (Critique)

Le serveur **DOIT** avoir une table `outputs` ou similaire :

```sql
CREATE TABLE IF NOT EXISTS outputs (
    sensor VARCHAR(50) PRIMARY KEY,
    pump_aqua TINYINT(1) DEFAULT 0,
    pump_tank TINYINT(1) DEFAULT 0,
    heat TINYINT(1) DEFAULT 0,
    light TINYINT(1) DEFAULT 0,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);
```

Et le POST doit faire l'upsert :
```php
// Mettre à jour état heat
$heat = intval($_POST['etatHeat'] ?? 0);
$sql = "INSERT INTO outputs (sensor, heat) VALUES (?, ?)
        ON DUPLICATE KEY UPDATE heat = VALUES(heat)";
$stmt->bind_param("si", $sensor, $heat);
```

---

## 📊 Logs Attendus (v11.36 avec fix)

### Séquence Correcte

```log
T+0s: Activation locale
[Web] 🔥 Starting heater...
[INFO] Heater GPIO2 ON
[Persistence] État heater=ON sauvegardé en NVS

T+0.5s: POST serveur
[HTTP] → post-data-test (etatHeat=1)
[HTTP] ← code 200

T+5s: GET remote state
[HTTP] → GET remote state
[Network] === JSON REÇU DU SERVEUR ===
{
  "heat": "1",    ← ✅ Mis à jour par serveur !
  ...
}
[Network] Chauffage déjà ON - pas de changement ✓
```

### Séquence Protégée (Si serveur pas à jour)

```log
T+5s: GET remote state
[Network] === JSON REÇU DU SERVEUR ===
{
  "heat": "0",    ← ❌ Pas mis à jour
  ...
}

[Network] ⚠️ État distant chauffage (OFF) diffère du local (ON)
[Network] ⚠️ Action locale récente (5234 ms), conserve local
[Network] État distant chauffage non fiable, conserve local
→ Chauffage reste ON ✓
```

---

## 🎯 Recommandations Urgentes

### Action 1: **Vérifier Serveur Distant** 🔍 PRIORITÉ 1

Vérifier si `post-data-test` met à jour table outputs :

```bash
# Tester avec curl
curl -X POST http://iot.olution.info/ffp3/post-data-test \
  -d "api_key=fdGTMoptd5CD2ert3" \
  -d "sensor=esp32-wroom" \
  -d "version=11.35" \
  -d "etatHeat=1" \
  -d "TempAir=25.0"

# Puis vérifier BDD
SELECT heat FROM outputs WHERE sensor='esp32-wroom';
```

Si `heat` n'est PAS mis à jour → **Bug serveur à corriger**

### Action 2: **Implémenter v11.36** 🔧 PRIORITÉ 2

Code ESP32 de protection contre états distants non fiables.

### Action 3: **Logs Diagnostics** 📊

Ajouter dans `handleRemoteActuators()` :

```cpp
// Log comparaison local vs distant
bool localState = AutomatismPersistence::loadCurrentActuatorState("heater", false);
bool remoteState = isTrue(doc["heat"]);

Serial.printf("[Network] 🔍 HEAT - Local NVS: %s, Distant: %s, Match: %s\n",
             localState ? "ON" : "OFF",
             remoteState ? "ON" : "OFF",
             localState == remoteState ? "✓" : "✗");
```

---

## 📝 Conclusion Provisoire

### Problème Probable

**Le serveur distant ne persiste PAS les états actionneurs** dans une table consultable par GET remote state.

**Flux actuel** (BUG):
```
POST → ffp3Data2 (historique) ✓
POST → outputs (états) ✗ ← Pas fait !
GET  → outputs (états) → Retourne ancien état ✗
ESP32 applique ancien état → Chauffage OFF ✗
```

**Flux attendu** (CORRECT):
```
POST → ffp3Data2 (historique) ✓
POST → outputs (états) ✓ ← À corriger serveur
GET  → outputs (états) → Retourne nouvel état ✓
ESP32 voit état cohérent → Chauffage reste ON ✓
```

---

## 🚀 Action Immédiate

**Je dois**:
1. 🔍 Vérifier le code serveur PHP dans `ffp3/post-data-test`
2. 🔧 Corriger le serveur pour persister les états actionneurs
3. 🛡️ Implémenter v11.36 ESP32 avec protection renforcée

**Veux-tu que je**:
- Examine le code serveur PHP ?
- Implémente la protection v11.36 côté ESP32 ?
- Les deux ?

---

**Status**: ⚠️ **BUG IDENTIFIÉ - Correction requise**  
**Priorité**: 🔴 HAUTE (fonctionnalité utilisateur bloquée)  
**Prochaine étape**: Investigation code serveur PHP


