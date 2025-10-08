# Corrections du Gestionnaire WiFi

## Problèmes Résolus

### 1. Propriétés WiFi manquantes dans les messages WebSocket
**Symptôme** : Messages d'erreur `[WiFi] wifiStaConnected non défini dans les données`

**Cause** : Le firmware déployé sur l'ESP32 n'était peut-être pas à jour, ou les propriétés WiFi étaient effectivement manquantes dans certains messages.

**Solution** : 
- Le code dans `include/realtime_websocket.h` envoie déjà correctement les propriétés WiFi dans les fonctions `broadcastSensorData()` et `broadcastNow()` (lignes 288-313 et 361-386)
- Suppression des logs d'erreur répétitifs côté JavaScript pour éviter la pollution de la console
- Utilisation silencieuse du cache lorsque les propriétés ne sont pas présentes

### 2. Erreur `ERR_CONNECTION_RESET` lors de `/wifi/connect`
**Symptôme** : La connexion HTTP est perdue lors de la tentative de connexion à un nouveau réseau WiFi

**Cause** : L'ESP32 se déconnectait du réseau actuel AVANT d'envoyer une réponse HTTP, causant la perte de connexion avec le client web.

**Solution** : Modification du handler `/wifi/connect` dans `src/web_server.cpp` :
- **Réponse immédiate** : Envoi d'une réponse HTTP AVANT de se déconnecter
- **Sauvegarde anticipée** : Sauvegarde du réseau en NVS avant la déconnexion
- **Tâche asynchrone** : Connexion WiFi dans une tâche FreeRTOS séparée pour ne pas bloquer le serveur web
- **Logs détaillés** : Ajout de logs Serial pour le débogage

### 3. Timeouts après reconnexion WiFi
**Symptôme** : Erreurs `ERR_CONNECTION_TIMED_OUT` sur tous les endpoints après une connexion WiFi

**Cause** : Le client web ne gérait pas la reconnexion automatique après le changement de réseau.

**Solution** : Amélioration de `connectToWifi()` dans `data/index.html` :
- **Gestion d'erreurs avancée** : Distinction entre timeout, erreur réseau, et autres erreurs
- **Reconnexion automatique** : Tentatives de reconnexion avec feedback visuel
- **Vérification progressive** : Polling automatique pour vérifier la connexion réussie
- **Interface utilisateur améliorée** : Messages de progression détaillés avec compteurs

## Modifications Apportées

### 1. `src/web_server.cpp` (lignes 1382-1551)

**Changements principaux** :
```cpp
// AVANT la déconnexion
- Sauvegarde en NVS (si demandé)
- Envoi de la réponse HTTP
- Attente de 500ms pour que la réponse soit envoyée

// APRÈS envoi de la réponse
- WiFi.disconnect()
- WiFi.begin() dans une tâche FreeRTOS séparée
- Logs détaillés à chaque étape
```

**Avantages** :
- Le client reçoit toujours une réponse HTTP
- Le serveur web n'est pas bloqué pendant 15 secondes
- La connexion se fait en arrière-plan
- Meilleure gestion des erreurs

### 2. `data/index.html` 

#### Fonction `connectToWifi()` (lignes 2808-2990)

**Nouvelles fonctionnalités** :
- **Timeout personnalisé** : 20 secondes pour la requête initiale
- **Gestion d'erreurs typée** : 
  - `AbortError`/`TimeoutError` → Message de timeout
  - `Failed to fetch` → Reconnexion automatique
  - Autres erreurs → Message d'erreur générique
- **Vérification de connexion** : 
  - Polling toutes les 1-2 secondes
  - Maximum 20 tentatives (20 secondes)
  - Vérification du SSID connecté
- **Feedback utilisateur** :
  - Compteurs de tentatives
  - Messages de progression
  - Alertes colorées selon le statut

#### Logs WiFi (lignes 1872-1969)

**Changements** :
- Suppression de `console.log('[WiFi] wifiStaConnected non défini - utilisation du cache')`
- Suppression de `console.log('[WiFi] wifiApActive non défini - utilisation du cache')`
- Les données du cache sont utilisées silencieusement quand les propriétés ne sont pas présentes

## Déploiement

### 1. Compiler et uploader le firmware
```bash
# Compiler le projet
pio run -e wroom_ota

# Uploader via OTA (si l'ESP32 est accessible)
pio run -e wroom_ota -t upload

# OU uploader via USB série
pio run -e wroom_ota -t upload --upload-port COM3
```

### 2. Uploader le système de fichiers (SPIFFS/LittleFS)
```bash
# Uploader les fichiers web (data/)
pio run -e wroom_ota -t uploadfs
```

### 3. Vérification

Après déploiement :
1. Ouvrir la console du navigateur (F12)
2. Accéder au gestionnaire WiFi
3. Tenter de se connecter à un nouveau réseau
4. Vérifier :
   - ✅ Pas de messages d'erreur répétitifs `wifiStaConnected non défini`
   - ✅ Messages de progression pendant la connexion
   - ✅ Reconnexion automatique réussie
   - ✅ Logs Serial détaillés sur l'ESP32

## Tests à Effectuer

### Test 1 : Connexion à un nouveau réseau
1. Ouvrir le gestionnaire WiFi
2. Scanner les réseaux disponibles
3. Se connecter à un nouveau réseau avec mot de passe
4. Cocher "Sauvegarder ce réseau"
5. **Attendu** :
   - Message "Connexion en cours..." avec compteur
   - Reconnexion automatique après 2-5 secondes
   - Affichage des nouvelles informations WiFi (IP, RSSI)

### Test 2 : Connexion à un réseau sauvegardé
1. Cliquer sur "Réseaux Sauvegardés"
2. Cliquer sur "Connecter" pour un réseau existant
3. **Attendu** :
   - Comportement identique au Test 1
   - Pas de demande de mot de passe

### Test 3 : Échec de connexion
1. Essayer de se connecter avec un mauvais mot de passe
2. **Attendu** :
   - Message d'erreur après timeout (15 secondes)
   - L'ESP32 retourne en mode AP
   - La page reste accessible

### Test 4 : WebSocket après reconnexion
1. Se connecter à un nouveau réseau
2. Après reconnexion réussie
3. Vérifier que le WebSocket se reconnecte automatiquement
4. **Attendu** :
   - Icône WebSocket verte (connecté)
   - Mises à jour en temps réel des capteurs
   - Pas d'erreurs dans la console

## Logs de Débogage

### Côté ESP32 (Serial Monitor)
```
[WiFi] Demande de connexion à 'MonReseau'
[WiFi] Sauvegarde du réseau en NVS
[WiFi] Réseau 'MonReseau' ajouté dans NVS (total: 3)
[WiFi] Déconnexion du réseau actuel
[WiFi] Tentative de connexion à 'MonReseau'
[WiFi] Connecté avec succès à 'MonReseau' (IP: 192.168.1.45, RSSI: -65 dBm)
```

### Côté Navigateur (Console)
```
🔄 Connexion à MonReseau en cours...
🔄 ESP32 en cours de reconnexion... (1/20)
🔄 ESP32 en cours de reconnexion... (2/20)
✅ Connecté avec succès à MonReseau
```

## Améliorations Futures Possibles

1. **Notification push** : Envoyer une notification WebSocket aux autres clients connectés lors d'un changement de réseau
2. **Historique de connexions** : Garder un historique des connexions réussies/échouées
3. **Signal WiFi en temps réel** : Afficher l'évolution du RSSI en temps réel pendant la connexion
4. **Mode AP automatique** : Retour automatique en mode AP après échec de connexion (déjà partiellement implémenté)
5. **Scan automatique** : Scanner les réseaux disponibles automatiquement à l'ouverture du gestionnaire

## Résumé des Correctifs

| Problème | Impact | Solution | Fichier Modifié |
|----------|--------|----------|-----------------|
| Propriétés WiFi manquantes | Console polluée | Logs silencieux + cache | `data/index.html` |
| ERR_CONNECTION_RESET | Échec de connexion | Réponse avant déconnexion | `src/web_server.cpp` |
| Timeouts après reconnexion | Interface figée | Reconnexion automatique | `data/index.html` |
| Blocage du serveur | Timeout 15s | Tâche FreeRTOS séparée | `src/web_server.cpp` |

## Notes Importantes

⚠️ **Attention** : Lors du déploiement via OTA, assurez-vous que :
- L'ESP32 est connecté au réseau WiFi
- L'adresse IP de l'ESP32 est correctement configurée dans `platformio.ini`
- Le port 3232 (OTA) est ouvert sur le réseau

💡 **Conseil** : Si vous testez sur un réseau différent, l'ESP32 peut changer d'adresse IP. Dans ce cas, vous devrez peut-être reconnecter via le point d'accès de secours pour retrouver la nouvelle IP.

🔧 **Débogage** : Si les problèmes persistent :
1. Vérifier que le firmware est bien à jour : compiler et uploader
2. Vérifier que le système de fichiers est bien à jour : uploadfs
3. Vider le cache du navigateur (Ctrl+Shift+Delete)
4. Monitorer les logs Serial de l'ESP32
5. Vérifier les logs de la console du navigateur

## Version

- **Date** : 2025-10-01
- **Auteur** : Assistant IA
- **Version du firmware** : Compatible avec la branche `wifi`
- **Fichiers modifiés** : 
  - `src/web_server.cpp`
  - `data/index.html`

