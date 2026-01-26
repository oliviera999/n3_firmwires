# 🔍 RAPPORT D'ANALYSE - Communication Serveur Distant

**Date:** 2026-01-25  
**Log analysé:** `monitor_wroom_test_2026-01-25_21-23-39.log`  
**Durée du log:** ~29 minutes (21:23:40 → 21:52:04)

---

## 📊 RÉSUMÉ EXÉCUTIF

### ❌ Problèmes identifiés

1. **Aucune donnée envoyée vers le serveur**
   - Aucun message `[SM]` (sendMeasurements)
   - Aucun message `[PR]` (postRaw)
   - Aucun message `[Sync]` (diagnostics de synchronisation)

2. **Aucune donnée reçue du serveur**
   - Aucun message `[GET]` (fetchRemoteState)
   - Aucun message `[GPIOParser]` (parsing des commandes)
   - Aucun message `[GPIO]` (commandes GPIO reçues)

3. **Messages de diagnostic absents**
   - Aucun message `[Sync] Diagnostic` (devrait apparaître toutes les 30s)
   - Aucun message `[Config] Net flags` (chargement configuration réseau)

### ✅ Éléments fonctionnels

- **WiFi connecté** : RSSI entre -68 et -79 dBm (acceptable à faible)
- **Capteurs opérationnels** : Lectures régulières des capteurs
- **NVS fonctionnel** : Sauvegardes de température en NVS

---

## 🔎 ANALYSE DÉTAILLÉE

### 1. Point d'entrée : `AutomatismSync::update()`

**Code analysé** (`src/automatism/automatism_sync.cpp:55-92`):

```cpp
void AutomatismSync::update(const SensorReadings& readings, SystemActuators& acts, Automatism& core) {
    static uint32_t lastLogMs = 0;
    uint32_t now = millis();
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    bool sendEnabled = _config.isRemoteSendEnabled();
    
    // Log diagnostic toutes les 30 secondes
    if (now - lastLogMs > 30000) {
        Serial.printf("[Sync] Diagnostic: WiFi=%s, SendEnabled=%s, ...\n", ...);
    }
    
    if (wifiConnected && sendEnabled) {
        if (intervalReached) {
            Serial.printf("[Sync] ✅ Conditions remplies, envoi POST...\n");
            sendFullUpdate(...);
        }
    } else {
        Serial.printf("[Sync] ⚠️ Envoi POST bloqué: WiFi=%s, SendEnabled=%s\n", ...);
    }
}
```

**Observation** : Aucun message `[Sync]` dans le log → **`AutomatismSync::update()` n'est probablement jamais appelé ou ne s'exécute pas**

### 2. Chaîne d'appels attendue

```
app_tasks.cpp:334 → Automatism::update()
automatism.cpp:93 → _network.update()  // AutomatismSync::update()
automatism_sync.cpp:55 → update() avec logs
```

**Vérification nécessaire** : Confirmer que `_network.update()` est bien appelé ligne 93 de `automatism.cpp`

### 3. Conditions de réception (`pollRemoteState`)

**Code analysé** (`src/automatism/automatism_sync.cpp:335-341`):

```cpp
bool AutomatismSync::pollRemoteState(ArduinoJson::JsonDocument& doc, uint32_t currentMillis) {
    if (!_config.isRemoteRecvEnabled()) return false;  // ⚠️ PISTE 1
    if (currentMillis - _lastRemoteFetch < REMOTE_FETCH_INTERVAL_MS) return false;
    if (WiFi.status() != WL_CONNECTED) return false;
    return fetchRemoteState(doc);
}
```

**Piste 1** : `isRemoteRecvEnabled()` retourne probablement `false`

### 4. Conditions d'envoi (`AutomatismSync::update`)

**Conditions requises** :
- ✅ WiFi connecté (confirmé dans le log)
- ❓ `isRemoteSendEnabled()` = `true` (non vérifié)
- ❓ Intervalle de 2 minutes écoulé (non vérifié)

---

## 🎯 PISTES DE DIAGNOSTIC

### 🔴 PISTE 1 : Configuration NVS désactivée (PRIORITÉ HAUTE)

**Hypothèse** : Les flags réseau sont désactivés dans la NVS

**Vérifications à faire** :

1. **Dumper la NVS pour vérifier les flags** :
   ```bash
   # Via interface web ou script de dump NVS
   # Vérifier les clés :
   # - SYSTEM::net_send_en (doit être true)
   # - SYSTEM::net_recv_en (doit être true)
   ```

2. **Ajouter des logs au démarrage** :
   - Vérifier que `ConfigManager::loadNetworkFlags()` est appelé
   - Vérifier les valeurs chargées depuis NVS

3. **Forcer l'activation via interface web** :
   - Accéder à l'interface web locale
   - Vérifier/activer les options "Envoi distant" et "Réception distante"

**Code à vérifier** : `src/config.cpp:47-61` (`loadNetworkFlags()`)

---

### 🟡 PISTE 2 : `AutomatismSync::update()` non appelé (PRIORITÉ MOYENNE)

**Hypothèse** : La méthode `update()` n'est jamais invoquée

**Vérifications à faire** :

1. **Ajouter un log au début de `AutomatismSync::update()`** :
   ```cpp
   void AutomatismSync::update(...) {
       Serial.println(F("[Sync] ⚠️ DEBUG: update() appelé"));
       // ... reste du code
   }
   ```

2. **Vérifier l'appel dans `automatism.cpp:93`** :
   ```cpp
   _network.update(r, _acts, *this);  // Cette ligne est-elle exécutée ?
   ```

3. **Vérifier que `Automatism::update()` est appelé** :
   - Dans `app_tasks.cpp:334` : `g_ctx->automatism.update(readings);`
   - Confirmer que la tâche `autoTask` s'exécute

**Test rapide** : Ajouter un `Serial.println()` au début de chaque méthode de la chaîne d'appels

---

### 🟡 PISTE 3 : Messages de log filtrés ou non capturés (PRIORITÉ MOYENNE)

**Hypothèse** : Les messages sont générés mais non visibles dans le log

**Vérifications à faire** :

1. **Vérifier le niveau de log** :
   - Les messages `[Sync]` utilisent `Serial.printf()` → devraient apparaître
   - Vérifier si un filtre de log est actif

2. **Vérifier le début du log** :
   - Le log commence à 21:23:40 (pas de messages de boot)
   - Les messages de configuration peuvent être avant cette heure

3. **Capturer un nouveau log depuis le boot** :
   - Redémarrer l'ESP32
   - Capturer tous les messages depuis le démarrage

---

### 🟢 PISTE 4 : Intervalle d'envoi non atteint (PRIORITÉ BASSE)

**Hypothèse** : L'intervalle de 2 minutes n'est pas encore écoulé

**Vérifications à faire** :

1. **Vérifier `SEND_INTERVAL_MS`** :
   - Valeur par défaut : 120000 ms (2 minutes)
   - Vérifier dans `include/automatism/automatism_sync.h`

2. **Vérifier `_lastSend`** :
   - Si `_lastSend` est initialisé à une valeur future, l'envoi sera bloqué
   - Vérifier l'initialisation dans `begin()`

**Note** : Cette piste est peu probable car le log fait 29 minutes et aucun envoi n'est observé

---

### 🟢 PISTE 5 : Problème de mutex TLS (PRIORITÉ BASSE)

**Hypothèse** : Le mutex TLS est toujours occupé, bloquant les requêtes HTTP

**Vérifications à faire** :

1. **Vérifier les messages de mutex** :
   - Chercher `[GET] ⛔ Mutex TLS non disponible`
   - Chercher `[PR] ⛔ Impossible d'acquérir mutex TLS`

2. **Vérifier les conflits avec SMTP** :
   - Si des mails sont en cours d'envoi, le mutex peut être occupé
   - Vérifier les messages `[Mail]` dans le log

**Note** : Aucun message de mutex trouvé dans le log → cette piste est peu probable

---

## 🛠️ PLAN D'ACTION RECOMMANDÉ

### Étape 1 : Vérification immédiate (5 min)

1. **Dumper la NVS** pour vérifier `net_send_en` et `net_recv_en`
2. **Ajouter un log de debug** au début de `AutomatismSync::update()`
3. **Vérifier l'interface web locale** pour l'état des flags réseau

### Étape 2 : Diagnostic approfondi (15 min)

1. **Ajouter des logs de diagnostic** :
   ```cpp
   // Dans AutomatismSync::update()
   Serial.printf("[Sync] DEBUG: update() appelé, WiFi=%d, SendEnabled=%d\n", 
                 WiFi.status(), _config.isRemoteSendEnabled());
   ```

2. **Vérifier la chaîne d'appels** :
   - Ajouter des logs dans `Automatism::update()` ligne 93
   - Ajouter des logs dans `app_tasks.cpp` ligne 334

3. **Capturer un nouveau log depuis le boot complet**

### Étape 3 : Correction (selon résultats)

- **Si PISTE 1 confirmée** : Activer les flags dans NVS
- **Si PISTE 2 confirmée** : Vérifier pourquoi `update()` n'est pas appelé
- **Si PISTE 3 confirmée** : Ajuster la capture des logs

---

## 📝 CODE DE DIAGNOSTIC À AJOUTER

### Dans `src/automatism/automatism_sync.cpp` :

```cpp
void AutomatismSync::update(const SensorReadings& readings, SystemActuators& acts, Automatism& core) {
    // AJOUT: Log d'entrée pour confirmer l'appel
    static bool firstCall = true;
    if (firstCall) {
        Serial.println(F("[Sync] ⚠️ DEBUG: update() appelé pour la première fois"));
        Serial.printf("[Sync] DEBUG: WiFi=%d, SendEnabled=%d, RecvEnabled=%d\n",
                      WiFi.status() == WL_CONNECTED,
                      _config.isRemoteSendEnabled(),
                      _config.isRemoteRecvEnabled());
        firstCall = false;
    }
    
    // ... reste du code existant
}
```

### Dans `src/automatism.cpp` ligne 93 :

```cpp
// 4.3 Envoi périodique des données capteurs (toutes les 2 minutes)
Serial.printf("[Auto] DEBUG: Appel _network.update() à %lu ms\n", now);
_network.update(r, _acts, *this);
```

### Dans `src/config.cpp` ligne 47 :

```cpp
void ConfigManager::loadNetworkFlags() {
    Serial.println(F("[Config] 📥 Chargement flags réseau depuis NVS centralisé"));
    
    g_nvsManager.loadBool(NVS_NAMESPACES::SYSTEM, "net_send_en", _remoteSendEnabled, true);
    g_nvsManager.loadBool(NVS_NAMESPACES::SYSTEM, "net_recv_en", _remoteRecvEnabled, true);
    
    // AJOUT: Log détaillé des valeurs chargées
    Serial.printf("[Config] ✅ Net flags - send:%s recv:%s (NVS: send=%d, recv=%d)\n",
                  _remoteSendEnabled?"ON":"OFF",
                  _remoteRecvEnabled?"ON":"OFF",
                  _remoteSendEnabled, _remoteRecvEnabled);
}
```

---

## 📈 MÉTRIQUES ATTENDUES

Une fois corrigé, vous devriez voir dans les logs :

1. **Toutes les 30 secondes** :
   ```
   [Sync] Diagnostic: WiFi=OK, SendEnabled=YES, TimeSinceLastSend=XXXX ms, Interval=120000 ms, Ready=YES/NO
   ```

2. **Toutes les 2 minutes** :
   ```
   [Sync] ✅ Conditions remplies, envoi POST... (dernier envoi il y a XXXX ms)
   [SM] === DÉBUT SENDMEASUREMENTS ===
   [PR] === DÉBUT POSTRAW ===
   ```

3. **Toutes les 30 secondes (réception)** :
   ```
   [GET] === DÉBUT REQUÊTE GET REMOTE STATE ===
   [GPIOParser] === PARSING JSON SERVEUR ===
   ```

---

## ✅ CONCLUSION

**Problème principal identifié** : Aucune communication avec le serveur distant (ni envoi, ni réception)

**Cause probable** : Configuration NVS désactivée (`net_send_en` et/ou `net_recv_en` = `false`)

**Action immédiate recommandée** : Vérifier et activer les flags réseau dans la NVS

---

**Rapport généré le:** 2026-01-25  
**Analyse effectuée par:** Assistant IA (Auto)
