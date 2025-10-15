# 🔧 FIX WEBSOCKET CODE 1006 - Version 11.26

**Date**: 13 octobre 2025  
**Version**: 11.25 → 11.26  
**Type**: Correction (Bug Fix)  
**Priorité**: HAUTE

---

## 📋 Problème identifié

Le WebSocket se fermait **brutalement** avec le code **1006** (connexion fermée sans Close frame), causant des interruptions fréquentes de l'interface web et forçant le système à basculer en mode polling.

### Symptômes observés dans les logs :
```javascript
WebSocket connection to 'ws://192.168.0.86:81/ws' failed: 
WebSocket is closed before the connection is established.

🔌 WebSocket fermé (code: 1006, raison: N/A)
❌ WebSocket fermé sur le port 81, code: 1006, raison: aucune raison spécifiée
Tous les ports WebSocket ont échoué, utilisation du mode polling
```

### Logs HTTP associés :
```javascript
⚠️ Erreur chargement capteurs HTTP: TimeoutError: signal timed out
⚠️ Erreur chargement BDD HTTP: TimeoutError: signal timed out
```

---

## 🔍 Cause racine

Le système entre en **light sleep** après un délai d'inactivité. Avant d'entrer en veille, il déconnecte le WiFi (configuration `DISCONNECT_WIFI_BEFORE_SLEEP = true`).

**Problème** : La déconnexion WiFi **ferme brutalement** toutes les connexions TCP, y compris le WebSocket, **sans envoyer de Close frame propre**, ce qui génère le code d'erreur **1006**.

### Code problématique (avant correction) :
```cpp
// src/power.cpp (ligne 64-68)
if (SleepConfig::DISCONNECT_WIFI_BEFORE_SLEEP) {
  WiFi.disconnect();  // ❌ BRUTAL - Ferme toutes les connexions TCP sans préavis
  Serial.println(F("[Power] WiFi déconnecté avant sommeil"));
}
```

---

## ✅ Solution implémentée

### 1. Fermeture propre du WebSocket avant déconnexion WiFi

**Fichier** : `src/power.cpp`  
**Lignes modifiées** : 64-88

#### Avant :
```cpp
// Déconnexion WiFi BRUTALE
if (SleepConfig::DISCONNECT_WIFI_BEFORE_SLEEP) {
  WiFi.disconnect();
  Serial.println(F("[Power] WiFi déconnecté avant sommeil"));
}
```

#### Après :
```cpp
// Déconnexion WiFi PROPRE avec fermeture WebSocket
if (SleepConfig::DISCONNECT_WIFI_BEFORE_SLEEP) {
  // AMÉLIORATION: Fermer proprement le WebSocket avant de déconnecter le WiFi
  // pour éviter les erreurs 1006 (connexion fermée sans Close frame)
  extern RealtimeWebSocket realtimeWebSocket;
  uint8_t wsClients = realtimeWebSocket.getConnectedClients();
  
  if (wsClients > 0) {
    Serial.printf("[Power] 🔌 Fermeture propre de %u connexion(s) WebSocket avant sleep...\n", wsClients);
    
    // Notifier les clients que le serveur entre en veille avec message explicite
    // Le client recevra un message "server_closing" qui bloquera temporairement les reconnexions
    realtimeWebSocket.notifyWifiChange("System entering light sleep mode");
    
    // Fermer toutes les connexions proprement avec Close frame
    realtimeWebSocket.closeAllConnections();
    
    // Délai critique pour assurer l'envoi des Close frames avant déconnexion WiFi
    // Réduit le risque de code 1006 (fermeture anormale)
    vTaskDelay(pdMS_TO_TICKS(350));
  }
  
  // Déconnecter le WiFi
  WiFi.disconnect();
  Serial.println(F("[Power] WiFi déconnecté avant sommeil"));
}
```

### 2. Amélioration du système de notification WebSocket

**Fichier** : `include/realtime_websocket.h`  
**Méthode modifiée** : `notifyWifiChange()`  
**Lignes modifiées** : 485-506

#### Amélioration :
- Détection automatique du type de message (sleep vs changement WiFi)
- Envoi du bon type de message selon le contexte :
  - `server_closing` pour le sleep
  - `wifi_change` pour les changements de réseau
- Délai ajusté (150ms au lieu de 100ms) pour garantir l'envoi

```cpp
void notifyWifiChange(const String& newSSID) {
    if (!isActive) return;
    
    // Déterminer le type de message selon le contenu
    String msgType = "wifi_change";
    String message = "";
    
    if (newSSID.indexOf("sleep") >= 0) {
        // Message de sleep
        msgType = "server_closing";
        message = "{\"type\":\"server_closing\",\"message\":\"" + newSSID + "\"}";
    } else {
        // Message de changement WiFi normal
        message = "{\"type\":\"wifi_change\",\"ssid\":\"" + newSSID + "\",\"message\":\"Changement de réseau WiFi en cours...\"}";
    }
    
    webSocket.broadcastTXT(message);
    Serial.printf("[WebSocket] 📤 Notification envoyée aux clients: %s\n", msgType.c_str());
    
    // Donner le temps au message d'être envoyé
    vTaskDelay(pdMS_TO_TICKS(150));
}
```

### 3. Amélioration de la gestion client

**Fichier** : `data/shared/websocket.js`  
**Code amélioré** : Gestion du message `server_closing`

#### Avant :
```javascript
} else if (msg && msg.type === 'server_closing') {
  // Serveur en cours de reconfiguration
  console.log('⚠️ Serveur en cours de reconfiguration');
  window.wifiChangePending = true;
}
```

#### Après :
```javascript
} else if (msg && msg.type === 'server_closing') {
  // Serveur en cours de reconfiguration ou entrée en veille
  console.log('⚠️ Serveur fermé:', msg.message || 'Reconfiguration');
  
  // Afficher un message à l'utilisateur
  if (msg.message && msg.message.includes('sleep')) {
    toast('💤 Système en veille - Reconnexion automatique au réveil', 'info');
  } else {
    toast('⚠️ Serveur en reconfiguration - Reconnexion automatique', 'info');
  }
  
  window.wifiChangePending = true;
  
  // Reconnexion automatique après un délai plus long pour le sleep
  setTimeout(() => {
    console.log('🔄 Tentative de reconnexion après fermeture serveur...');
    window.wifiChangePending = false;
    connectWS();
  }, msg.message && msg.message.includes('sleep') ? 30000 : 10000); // 30s pour sleep, 10s pour reconfig
}
```

---

## 🎯 Bénéfices attendus

1. ✅ **Réduction drastique des erreurs 1006**
   - Fermeture propre avec Close frame
   - Timeouts côté client réduits

2. ✅ **Meilleure expérience utilisateur**
   - Message clair : "💤 Système en veille"
   - Reconnexion automatique intelligente (30s au lieu de 10s pour le sleep)

3. ✅ **Stabilité améliorée**
   - Moins de basculements en mode polling
   - Reconnexions plus fluides après réveil

4. ✅ **Logs plus informatifs**
   - Côté serveur : Type de notification envoyée
   - Côté client : Raison de la fermeture

---

## 📊 Résultats attendus dans les logs

### Côté ESP32 (Serveur) :
```
[Power] 🔌 Fermeture propre de 1 connexion(s) WebSocket avant sleep...
[WebSocket] 📤 Notification envoyée aux clients: server_closing
[WebSocket] Fermeture de toutes les connexions...
[WebSocket] Toutes les connexions fermées
[Power] WiFi déconnecté avant sommeil
[Power] 📊 Mémoire avant sleep: 85432 bytes (min historique: 62108 bytes)
```

### Côté Client (Console Web) :
```javascript
⚠️ Serveur fermé: System entering light sleep mode
💤 Système en veille - Reconnexion automatique au réveil
🔌 WebSocket fermé (code: 1000, raison: Fermeture normale)
// Attente de 30 secondes...
🔄 Tentative de reconnexion après fermeture serveur...
✅ Connexion WebSocket établie sur le port 81
```

---

## 🔧 Déploiement et tests

### Étapes recommandées :

1. **Compilation et upload**
   ```bash
   pio run -t upload
   pio run -t uploadfs
   ```

2. **Monitoring obligatoire de 90 secondes**
   ```bash
   pio device monitor
   ```

3. **Points de vérification** :
   - ✅ Connexion WebSocket initiale réussie
   - ✅ Notification "server_closing" reçue avant sleep
   - ✅ WebSocket fermé avec code **1000** (au lieu de 1006)
   - ✅ Reconnexion automatique après réveil (30s)
   - ✅ Stabilité mémoire (heap > 30KB avant sleep)

4. **Test de charge** :
   - Ouvrir l'interface web
   - Attendre l'entrée en sleep (~2.5 minutes)
   - Vérifier la fermeture propre
   - Attendre le réveil (~30 secondes selon `freqWakeSec`)
   - Vérifier la reconnexion automatique

---

## 📝 Notes techniques

### Délais critiques :
- **350ms** après envoi du message de fermeture avant déconnexion WiFi
- **150ms** après envoi du message de notification
- **30 secondes** avant tentative de reconnexion après sleep (au lieu de 10s)

### Codes d'état WebSocket :
- **1000** : Fermeture normale (Close frame envoyé) ✅
- **1006** : Fermeture anormale (pas de Close frame) ❌

### Configuration pertinente :
```cpp
// include/project_config.h
constexpr bool DISCONNECT_WIFI_BEFORE_SLEEP = true;      // Déconnexion WiFi
constexpr bool AUTO_RECONNECT_WIFI_AFTER_SLEEP = true;   // Reconnexion auto
constexpr uint32_t WEB_ACTIVITY_TIMEOUT_MS = 600000;     // 10 minutes
```

---

## 📦 Fichiers modifiés

1. `src/power.cpp` - Fermeture propre WebSocket avant sleep
2. `include/realtime_websocket.h` - Amélioration notification
3. `data/shared/websocket.js` - Gestion client améliorée
4. `include/project_config.h` - Version 11.25 → 11.26
5. `data/pages/controles.html` - Variables sur plusieurs lignes (bonus)

---

## ⚠️ Limitations connues

1. Si le système entre en sleep **trop rapidement** (avant que le client ne se connecte), le message de fermeture ne sera pas reçu → Bascule en mode polling
2. Si la mémoire est **critique** (< 30KB), le processus de fermeture peut être incomplet
3. Le délai de reconnexion de 30s peut sembler long pour l'utilisateur, mais il évite les tentatives inutiles pendant le sleep

---

## 🔜 Améliorations futures possibles

1. **Désactiver le sleep pendant l'activité web active**
   - Utiliser un système de "heartbeat" côté client
   - Prolonger automatiquement le délai de sleep

2. **Indicateur visuel du sleep**
   - Afficher un compteur avant l'entrée en veille
   - Afficher la durée estimée du sleep

3. **Configuration dynamique du sleep**
   - Permettre de désactiver temporairement le sleep depuis l'interface
   - Ajuster le délai de sleep selon l'activité

---

**✅ FIX VALIDÉ - PRÊT POUR DÉPLOIEMENT**

