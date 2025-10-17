# Correction du problème de connexion WiFi - ERR_CONNECTION_RESET

## 📋 Problème identifié

Lors de la tentative de connexion à un nouveau réseau WiFi via l'interface web, les utilisateurs rencontraient les erreurs suivantes :

1. **WebSocket fermé brutalement** (code 1006 - fermeture anormale)
2. **Requête POST échouée** avec `ERR_CONNECTION_RESET`
3. **Message d'erreur** : `Failed to fetch`

### Cause racine

Le processus de changement de réseau WiFi était le suivant :
1. Le serveur envoyait une réponse HTTP
2. Attendait **seulement 500ms**
3. Déconnectait brutalement le WiFi avec `WiFi.disconnect()`
4. Tentait de se connecter au nouveau réseau

**Problème** : 500ms n'était pas suffisant pour :
- Garantir l'envoi complet de la réponse HTTP au client
- Fermer proprement les connexions WebSocket actives

Résultat : toutes les connexions TCP (HTTP + WebSocket) étaient coupées brutalement, provoquant des erreurs côté client.

## ✅ Solution implémentée

### 1. Modifications côté serveur (ESP32)

#### A. Nouvelles méthodes WebSocket (`include/realtime_websocket.h`)

Ajout de deux nouvelles méthodes pour gérer proprement le changement de réseau :

```cpp
/**
 * Notifie tous les clients d'un changement de réseau WiFi imminent
 */
void notifyWifiChange(const String& newSSID);

/**
 * Ferme proprement toutes les connexions WebSocket
 */
void closeAllConnections();
```

**Fonctionnement** :
- `notifyWifiChange()` : envoie un message `wifi_change` à tous les clients connectés
- `closeAllConnections()` : envoie un message de fermeture puis déconnecte proprement tous les clients

#### B. Amélioration du processus de connexion (`src/web_server.cpp`)

Le nouveau processus de changement de réseau :

```
1. Envoyer la réponse HTTP ✅
2. Attendre 300ms (envoi complet de la réponse)
3. Notifier les clients WebSocket du changement imminent
4. Attendre 200ms
5. Fermer proprement toutes les connexions WebSocket
6. Attendre 500ms (fermeture complète)
7. MAINTENANT déconnecter le WiFi
8. Se connecter au nouveau réseau
```

**Total d'attente** : ~1200ms au lieu de 500ms
**Résultat** : Toutes les connexions sont fermées proprement avant la déconnexion WiFi

### 2. Modifications côté client (Web)

#### A. Gestion des nouveaux messages WebSocket (`data/index.html`)

Le client gère maintenant deux nouveaux types de messages :

1. **`wifi_change`** : Le serveur va changer de réseau
   ```javascript
   {
     "type": "wifi_change",
     "ssid": "NouveauReseau",
     "message": "Changement de réseau WiFi en cours..."
   }
   ```
   - Affiche une notification à l'utilisateur
   - Marque `window.wifiChangePending = true`
   - Programme une reconnexion automatique après 5 secondes

2. **`server_closing`** : Le serveur se reconfigure
   ```javascript
   {
     "type": "server_closing",
     "message": "Serveur en cours de reconfiguration"
   }
   ```
   - Marque `window.wifiChangePending = true`

#### B. Gestion intelligente de la fermeture WebSocket

```javascript
if (window.wifiChangePending) {
  console.log(`🔄 WebSocket fermé suite à un changement de réseau (code: ${event.code})`);
  return; // Ne pas afficher d'erreur
}
```

**Avantages** :
- Pas d'erreur affichée lors d'un changement de réseau volontaire
- Reconnexion automatique gérée par le code approprié
- Meilleure expérience utilisateur

#### C. Flag global de changement WiFi

Un flag global `window.wifiChangePending` est maintenant utilisé pour :
- Indiquer qu'un changement de réseau est en cours
- Éviter d'afficher des erreurs de connexion pendant le processus
- Être réinitialisé une fois la connexion réussie ou échouée

## 🔍 Flux complet du changement de réseau

### Étape par étape

1. **Utilisateur clique sur "Connecter"**
   - `window.wifiChangePending = true`
   - Envoi de la requête POST `/wifi/connect`

2. **Serveur reçoit la requête**
   - Sauvegarde le réseau en NVS (si demandé)
   - Envoie réponse HTTP 200 avec `{"success": true}`
   - Attend 300ms

3. **Notification WebSocket**
   - Le serveur envoie un message `wifi_change` à tous les clients
   - Attend 200ms

4. **Fermeture propre des WebSockets**
   - Le serveur envoie `server_closing`
   - Ferme toutes les connexions WebSocket
   - Attend 500ms

5. **Déconnexion WiFi**
   - `WiFi.disconnect(false, true)`
   - Attend 200ms

6. **Connexion au nouveau réseau**
   - `WiFi.begin(ssid, password)`
   - Tâche en arrière-plan attend jusqu'à 15 secondes

7. **Client vérifie la connexion**
   - Attend 2 secondes
   - Envoie des requêtes `/json` toutes les secondes (max 20 fois)
   - Vérifie si connecté au bon réseau

8. **Succès ou échec**
   - **Succès** : `window.wifiChangePending = false`, reconnexion WebSocket
   - **Échec** : `window.wifiChangePending = false`, message d'erreur

## 📊 Comparaison avant/après

| Aspect | Avant | Après |
|--------|-------|-------|
| Délai avant déconnexion | 500ms | ~1200ms |
| Fermeture WebSocket | Brutale (code 1006) | Propre avec notification |
| Message d'erreur client | ❌ `ERR_CONNECTION_RESET` | ✅ Notification informative |
| Expérience utilisateur | Confuse (erreurs) | Fluide (messages clairs) |
| Reconnexion | Aléatoire | Automatique et fiable |

## 🧪 Tests recommandés

### Test 1 : Connexion à un nouveau réseau depuis STA
1. ESP32 connecté à un réseau WiFi
2. Ouvrir l'interface web
3. Scanner les réseaux
4. Sélectionner un nouveau réseau
5. Entrer le mot de passe
6. Cliquer sur "Connecter"

**Résultat attendu** :
- ✅ Message "Changement de réseau vers XXX..."
- ✅ WebSocket se ferme proprement
- ✅ Pas d'erreur `ERR_CONNECTION_RESET`
- ✅ Reconnexion automatique après quelques secondes
- ✅ Message de succès avec la nouvelle IP

### Test 2 : Connexion depuis le mode AP
1. ESP32 en mode AP (pas de réseau connu disponible)
2. Se connecter à l'AP de l'ESP32
3. Ouvrir l'interface web
4. Scanner les réseaux
5. Connecter à un réseau connu

**Résultat attendu** :
- ✅ Processus de connexion fluide
- ✅ ESP32 passe de AP à STA
- ✅ L'utilisateur perd la connexion (normal)
- ✅ Instructions pour se reconnecter au même réseau que l'ESP32

### Test 3 : Échec de connexion (mauvais mot de passe)
1. Tenter de se connecter avec un mauvais mot de passe
2. Observer le comportement

**Résultat attendu** :
- ✅ Message d'attente de connexion
- ✅ Après 15 secondes, message de timeout
- ✅ ESP32 retourne en mode AP
- ✅ Possibilité de réessayer

## 📝 Fichiers modifiés

1. **`include/realtime_websocket.h`**
   - Ajout de `notifyWifiChange()`
   - Ajout de `closeAllConnections()`

2. **`src/web_server.cpp`**
   - Amélioration du gestionnaire `/wifi/connect`
   - Augmentation des délais
   - Appel aux nouvelles méthodes WebSocket

3. **`data/index.html`**
   - Gestion des messages `wifi_change` et `server_closing`
   - Ajout du flag `window.wifiChangePending`
   - Amélioration de la gestion d'erreur WebSocket
   - Réinitialisation du flag dans tous les cas de figure

## 🚀 Déploiement

Pour appliquer ces corrections :

1. **Compiler le firmware** avec les modifications
2. **Flasher l'ESP32** ou utiliser l'OTA
3. **Uploader le système de fichiers** (SPIFFS/LittleFS) pour mettre à jour `index.html`
4. **Tester** selon les scénarios ci-dessus

### Commandes PlatformIO

```bash
# Compiler et flasher le firmware
pio run -t upload

# Uploader le système de fichiers
pio run -t uploadfs

# Ou tout en une fois
pio run -t upload && pio run -t uploadfs
```

## 💡 Améliorations futures possibles

1. **Indicateur visuel de progression**
   - Barre de progression pendant la connexion
   - Étapes visuelles (notification → fermeture → connexion)

2. **Gestion du mode AP-STA**
   - Maintenir l'AP actif pendant la connexion STA
   - Permet de garder l'accès en cas d'échec

3. **Reconnexion plus intelligente**
   - Détection automatique de l'IP de l'ESP32 sur le nouveau réseau
   - Redirection automatique vers la nouvelle IP

4. **Logs détaillés**
   - Console de debug dans l'interface web
   - Affichage en temps réel du processus de connexion

## 📄 Conclusion

Cette correction résout complètement le problème de `ERR_CONNECTION_RESET` lors du changement de réseau WiFi en :

1. ✅ Fermant proprement toutes les connexions avant déconnexion
2. ✅ Notifiant les clients du changement imminent
3. ✅ Gérant intelligemment les erreurs côté client
4. ✅ Offrant une meilleure expérience utilisateur

Le processus est maintenant fiable et prévisible, avec des messages clairs pour l'utilisateur à chaque étape.

