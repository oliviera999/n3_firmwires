# 🔧 Correction des Timeouts WebSocket et PANIC Relais

**Date**: 2025-01-01  
**Problème**: ESP32 qui redémarre avec PANIC lors de l'activation des relais (chauffage/lumière)  
**Cause**: Deadlock avec mutex `portMAX_DELAY` + timeouts WebSocket trop courts

---

## 🚨 **PROBLÈMES IDENTIFIÉS**

### 1. **Deadlock Mutex Critique**
- `broadcastNow()` utilisait `portMAX_DELAY` avec mutex
- Si le mutex était pris par une autre tâche → **DEADLOCK** → **PANIC** → **REBOOT**
- Même problème dans `EventLog` avec 3 fonctions utilisant `portMAX_DELAY`

### 2. **Timeouts WebSocket Insuffisants**
- Client attendait 3 secondes pour confirmation WebSocket
- Serveur pouvait prendre plus de temps sous charge
- Fallback HTTP activé trop souvent

### 3. **Feedback Non Spécifique**
- `broadcastNow()` envoyait toutes les données
- Client ne recevait pas de confirmation spécifique à l'action
- Interface utilisateur restait en "loading" indéfiniment

---

## ✅ **CORRECTIONS APPLIQUÉES**

### **1. Correction des Mutex (Anti-Deadlock)**

#### `include/realtime_websocket.h`
```cpp
// AVANT (DANGEREUX)
xSemaphoreTake(mutex, portMAX_DELAY);

// APRÈS (SÉCURISÉ)
if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    Serial.println("[WebSocket] ⚠️ Mutex timeout dans broadcastNow(), skip");
    return;
}
```

#### `src/event_log.cpp` (3 fonctions corrigées)
```cpp
// AVANT (DANGEREUX)
xSemaphoreTake(s_mutex, portMAX_DELAY);

// APRÈS (SÉCURISÉ)
if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    Serial.println("[EventLog] ⚠️ Mutex timeout dans [fonction], skip");
    return;
}
```

### **2. Nouvelle Méthode de Confirmation Immédiate**

#### `include/realtime_websocket.h`
```cpp
/**
 * Envoie une confirmation d'action immédiate (pour éviter les timeouts)
 */
void sendActionConfirm(const String& action, const String& result) {
    if (!isActive) return;
    
    // Message léger et ciblé pour confirmation immédiate
    String json = "{\"type\":\"action_confirm\",\"action\":\"" + action + 
                  "\",\"result\":\"" + result + "\",\"timestamp\":" + 
                  String(millis()) + "}";
    
    // Envoi direct sans mutex pour éviter les deadlocks
    webSocket.broadcastTXT(json);
    Serial.printf("[WebSocket] ✅ Confirmation action: %s = %s\n", action.c_str(), result.c_str());
}
```

### **3. Utilisation des Confirmations Spécifiques**

#### `src/web_server.cpp`
```cpp
// AVANT (GÉNÉRIQUE)
realtimeWebSocket.broadcastNow();

// APRÈS (SPÉCIFIQUE)
realtimeWebSocket.sendActionConfirm("heater", resp);
realtimeWebSocket.sendActionConfirm("light", resp);
```

### **4. Client Web Amélioré**

#### `data/index.html`
```javascript
// AVANT (3 secondes)
setTimeout(() => { ... }, 3000);

// APRÈS (5 secondes)
setTimeout(() => { ... }, 5000);

// NOUVEAU : Écoute des confirmations spécifiques
if (!feedbackReceived && data.type === 'action_confirm' && data.action === name) {
    // Confirmation immédiate reçue
    feedbackReceived = true;
    clearTimeout(websocketTimeout);
    // Mise à jour interface...
}
```

---

## 🎯 **RÉSULTATS ATTENDUS**

### **1. Élimination des PANIC**
- ✅ Plus de deadlock avec mutex
- ✅ Timeouts sécurisés (50-100ms au lieu d'infini)
- ✅ Système robuste même sous charge

### **2. Feedback Immédiat**
- ✅ Confirmations spécifiques en < 100ms
- ✅ Interface utilisateur réactive
- ✅ Plus d'icônes qui tournent indéfiniment

### **3. Stabilité Améliorée**
- ✅ Timeout WebSocket augmenté à 5 secondes
- ✅ Fallback HTTP plus rare
- ✅ Système plus résilient

---

## 🧪 **TESTS RECOMMANDÉS**

### **Test 1: Activation Chauffage**
1. Activer le chauffage via l'interface web
2. Vérifier que l'icône s'affiche immédiatement avec ✅
3. Vérifier qu'aucun timeout n'apparaît dans la console
4. Vérifier qu'aucun reboot ne se produit

### **Test 2: Activation Lumière**
1. Activer la lumière via l'interface web
2. Vérifier que l'icône s'affiche immédiatement avec ✅
3. Vérifier qu'aucun timeout n'apparaît dans la console
4. Vérifier qu'aucun reboot ne se produit

### **Test 3: Activation Séquentielle**
1. Activer chauffage puis lumière rapidement
2. Vérifier que les deux actions se confirment
3. Vérifier qu'aucun deadlock ne se produit
4. Vérifier la stabilité du système

---

## 📊 **MONITORING**

### **Logs à Surveiller**
```
✅ Confirmation action: heater = HEATER ON
✅ Confirmation action: light = LIGHT ON
```

### **Logs à Éviter**
```
⚠️ Mutex timeout dans broadcastNow(), skip
Timeout WebSocket pour le relay heater, utilisation du feedback HTTP
[RESTART INFO] Raison du redémarrage: PANIC (erreur critique)
```

---

## 🔄 **ROLLBACK SI PROBLÈME**

Si les corrections causent des problèmes :

1. **Restaurer les mutex** :
   ```cpp
   xSemaphoreTake(mutex, portMAX_DELAY);
   ```

2. **Restaurer broadcastNow()** :
   ```cpp
   realtimeWebSocket.broadcastNow();
   ```

3. **Restaurer timeout** :
   ```javascript
   setTimeout(() => { ... }, 3000);
   ```

---

## 📝 **NOTES TECHNIQUES**

- **Timeout mutex** : 50-100ms suffisant pour éviter les deadlocks
- **Timeout WebSocket** : 5 secondes donne plus de marge
- **Confirmations spécifiques** : Messages légers, pas de mutex
- **Fallback HTTP** : Toujours disponible en cas d'échec WebSocket

**Impact** : Corrections minimales, compatibilité préservée, stabilité améliorée.
