# Améliorations du Debug du Serveur Local

## Résumé des Modifications

Ce document décrit les améliorations apportées au système de debug du serveur local pour permettre un diagnostic fin et une optimisation du code général.

## 🎯 Objectifs

- **Diagnostic fin** : Logs détaillés pour comprendre précisément ce qui se passe
- **Optimisation** : Identification des goulots d'étranglement et des problèmes de performance
- **Maintenance** : Facilitation du débogage et de la maintenance du système

## 📊 Améliorations Apportées

### 1. Serveur Web (`src/web_server.cpp`)

#### Logs Détaillés pour les Requêtes
- **Adresse IP du client** : Affichage de l'origine des requêtes
- **État de la mémoire** : Monitoring du heap avant et après traitement
- **Statut des opérations** : Confirmation visuelle des succès/échecs avec emojis

#### Endpoints d'Action (`/action`)
- **Logs de commandes** : Traçage détaillé de chaque commande reçue
- **Feedback immédiat** : Confirmation des actions avec logs explicites
- **Gestion des erreurs** : Messages d'erreur clairs et informatifs

#### Endpoint JSON (`/json`)
- **Monitoring du pool JSON** : Détection des épuisements de mémoire
- **Fallback intelligent** : Logs lors de l'utilisation du fallback
- **Taille des réponses** : Monitoring de la taille des données envoyées

#### Nouveaux Endpoints de Debug
- **`/server-status`** : État complet du serveur avec informations WiFi et WebSocket
- **`/debug-logs`** : Informations détaillées sur tous les systèmes

### 2. WebSocket (`src/realtime_websocket.cpp`)

#### Activité Client
- **Détection d'activité** : Logs du nombre de clients connectés
- **Notifications** : Traçage des interactions client-serveur

### 3. Gestionnaire WiFi (`src/wifi_manager.cpp`)

#### Processus de Connexion
- **Scan des réseaux** : Liste détaillée des réseaux trouvés avec RSSI
- **Tentatives de connexion** : Logs de chaque tentative avec détails techniques
- **Succès/Échecs** : Confirmation visuelle des résultats
- **AP de secours** : Logs du démarrage et de la configuration

#### Gestion des Erreurs
- **Réseaux rejetés** : Raisons du rejet (RSSI trop faible)
- **Tentatives multiples** : Logs des tentatives génériques après échec BSSID

### 4. Système de Diagnostics (`src/diagnostics.cpp`)

#### Monitoring de la Mémoire
- **Sauvegarde intelligente** : Logs des décisions de sauvegarde
- **Différences significatives** : Détection des pertes importantes de mémoire
- **Optimisation NVS** : Réduction des écritures inutiles

### 5. Automatisme (`src/automatism.cpp`)

#### Commandes Manuelles
- **Démarrage/Arrêt** : Logs détaillés des actions sur les actionneurs
- **Synchronisation** : Traçage des tentatives de synchronisation serveur
- **État WiFi** : Gestion des cas sans connexion

## 🔧 Nouveaux Endpoints de Debug

### `/server-status`
Retourne l'état complet du serveur :
```json
{
  "heapFree": 123456,
  "heapTotal": 234567,
  "psramFree": 345678,
  "psramTotal": 456789,
  "uptime": 12345678,
  "wifiStatus": 3,
  "wifiSSID": "MonReseau",
  "wifiIP": "192.168.1.100",
  "wifiRSSI": -45,
  "webSocketClients": 2,
  "forceWakeup": true
}
```

### `/debug-logs`
Informations détaillées sur tous les systèmes :
```json
{
  "system": {
    "uptime": 12345678,
    "freeHeap": 123456,
    "heapSize": 234567
  },
  "wifi": {
    "status": 3,
    "ssid": "MonReseau",
    "ip": "192.168.1.100",
    "rssi": -45
  },
  "websocket": {
    "connectedClients": 2,
    "isActive": true
  },
  "automatism": {
    "forceWakeup": true,
    "emailEnabled": true
  },
  "sensors": {
    "tempWater": 25.5,
    "tempAir": 22.3,
    "humidity": 65.0
  },
  "actuators": {
    "pumpAqua": false,
    "pumpTank": true,
    "heater": false,
    "light": true
  }
}
```

## 📈 Bénéfices

### Pour le Développement
- **Débogage facilité** : Logs clairs avec emojis pour identification rapide
- **Traçabilité** : Suivi complet des opérations et des erreurs
- **Performance** : Monitoring en temps réel des ressources

### Pour la Maintenance
- **Diagnostic rapide** : Identification immédiate des problèmes
- **Optimisation** : Détection des goulots d'étranglement
- **Prévention** : Alertes précoces sur les problèmes de mémoire

### Pour l'Utilisateur
- **Transparence** : Visibilité sur le fonctionnement du système
- **Fiabilité** : Meilleure gestion des erreurs et des états

## 🚀 Utilisation

### Monitoring en Temps Réel
1. Accéder à `http://[IP_ESP32]/server-status` pour l'état général
2. Accéder à `http://[IP_ESP32]/debug-logs` pour les détails complets
3. Surveiller les logs série pour le diagnostic en temps réel

### Diagnostic des Problèmes
1. **Mémoire** : Surveiller les logs `[Web] 📊 Heap libre`
2. **WiFi** : Vérifier les logs `[WiFi] 🔍` et `[WiFi] ✅`
3. **WebSocket** : Contrôler les logs `[WebSocket] 👤`
4. **Actions** : Suivre les logs `[Web] 🎮` et `[Auto] 🐠`

## 🔍 Exemples de Logs

### Requête Web
```
[Web] 🌐 Requête / depuis 192.168.1.50
[Web] 📁 Serving index.html from LittleFS
[Web] ✅ index.html sent successfully from LittleFS
[Web] 📊 Heap libre après traitement: 123456 bytes
```

### Action Manuelle
```
[Web] 🎮 Action request from 192.168.1.50
[Web] 🎯 Command: feedSmall
[Web] 🐟 Starting manual feed small...
[Auto] 🐠 Démarrage manuel pompe aquarium (local)...
[Auto] ✅ Pompe aquarium démarrée - Heap: 123456 bytes
[Web] ✅ Small feed completed successfully
```

### Connexion WiFi
```
[WiFi] 🔍 Balayage des réseaux...
[WiFi] 📡 3 réseaux trouvés:
  - MonReseau (-45dBm)
  - AutreReseau (-60dBm)
[WiFi] ✅ Réseau MonReseau accepté (RSSI: -45 dBm)
[WiFi] 🔗 Tentative (vu) MonReseau (RSSI -45 dBm, ch 6)...
[WiFi] ✅ Connecté à MonReseau (192.168.1.100, RSSI -45 dBm)
```

## 📝 Notes Techniques

- **Emojis** : Utilisés pour faciliter l'identification rapide des types de logs
- **Couleurs** : Les logs sont structurés pour faciliter le parsing automatique
- **Performance** : Les logs n'impactent pas les performances critiques
- **Mémoire** : Optimisation des allocations pour éviter les fuites

## 🔄 Maintenance Continue

Ces améliorations de debug permettent :
- **Surveillance proactive** des performances
- **Détection précoce** des problèmes
- **Optimisation continue** du code
- **Amélioration de la fiabilité** du système

Le système de debug est maintenant suffisamment détaillé pour permettre une optimisation fine et une maintenance efficace du serveur local.
