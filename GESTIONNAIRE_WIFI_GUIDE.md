# Gestionnaire WiFi FFP3 - Guide d'utilisation

## Vue d'ensemble

Le gestionnaire WiFi intégré permet de gérer les connexions WiFi de l'ESP32 directement depuis l'interface web. Il offre une solution complète pour scanner, connecter et sauvegarder des réseaux WiFi.

## Fonctionnalités

### ✅ Fonctionnalités implémentées

- **Scanner de réseaux** : Détection automatique des réseaux WiFi disponibles
- **Connexion manuelle** : Saisie de SSID et mot de passe pour connexion
- **Sauvegarde automatique** : Stockage des réseaux en NVS pour reconnexion après reboot
- **Gestion des réseaux sauvegardés** : Liste, connexion et suppression des réseaux stockés
- **Statut en temps réel** : Affichage du statut de connexion STA et AP
- **Interface moderne** : Design cohérent avec le reste du dashboard

### 🔧 Endpoints API

- `GET /wifi/scan` - Scanner les réseaux disponibles
- `GET /wifi/saved` - Lister les réseaux sauvegardés
- `POST /wifi/connect` - Se connecter à un réseau
- `POST /wifi/remove` - Supprimer un réseau sauvegardé
- `GET /wifi/status` - Obtenir le statut WiFi actuel

## Utilisation

### 1. Accès au gestionnaire

1. Ouvrir l'interface web de l'ESP32
2. Cliquer sur l'onglet **📶 WiFi** dans le menu principal
3. Le gestionnaire se charge automatiquement avec le statut actuel

### 2. Scanner les réseaux disponibles

1. Cliquer sur **🔍 Scanner les réseaux**
2. Attendre le scan (quelques secondes)
3. Les réseaux apparaissent avec :
   - Nom du réseau (SSID)
   - Force du signal (RSSI)
   - Type de sécurité (🔓 Ouvert / 🔒 Sécurisé)
   - Canal WiFi

### 3. Se connecter à un réseau

#### Méthode 1 : Depuis la liste scannée
1. Scanner les réseaux disponibles
2. Cliquer sur **🔗 Sélectionner** pour un réseau
3. Le SSID se remplit automatiquement
4. Saisir le mot de passe si nécessaire
5. Cocher **💾 Sauvegarder** pour stocker le réseau
6. Cliquer sur **🔗 Se connecter**

#### Méthode 2 : Saisie manuelle
1. Remplir le formulaire **Connexion Manuelle**
2. Saisir le SSID et le mot de passe
3. Cocher **💾 Sauvegarder** si souhaité
4. Cliquer sur **🔗 Se connecter**

### 4. Gérer les réseaux sauvegardés

Les réseaux sauvegardés apparaissent dans la section **💾 Réseaux Sauvegardés** :

- **🔗 Connecter** : Se connecter rapidement au réseau
- **🗑️ Supprimer** : Retirer le réseau de la liste sauvegardée

### 5. Surveiller le statut

La section **📶 Statut WiFi Actuel** affiche :
- État de la connexion STA (Connecté/Déconnecté)
- SSID du réseau connecté
- Adresse IP attribuée
- Force du signal (RSSI)
- État du mode AP
- Nombre de clients connectés à l'AP

## Stockage des données

### Format de stockage NVS

Les réseaux sont stockés dans le namespace `wifi_saved` :
- Clé `count` : Nombre total de réseaux sauvegardés
- Clé `net_X` : Réseau à l'index X (format: "SSID|password")

### Persistance

- Les réseaux sauvegardés persistent après un reboot
- Compatible avec le système de reconnexion automatique existant
- Les réseaux statiques de `secrets.h` restent prioritaires

## Sécurité

### Bonnes pratiques

- Les mots de passe sont stockés en clair dans NVS (limitation ESP32)
- Utiliser uniquement sur des réseaux de confiance
- Éviter de sauvegarder des réseaux publics non sécurisés

### Limitations

- Maximum ~20 réseaux sauvegardés (limitation mémoire NVS)
- Les mots de passe ne sont pas chiffrés
- Scan WiFi peut prendre 5-10 secondes

## Intégration technique

### Compatibilité

- Compatible avec le `WifiManager` existant
- Utilise le système de pool JSON pour l'optimisation mémoire
- Intégré au système de notifications toast
- Respecte le thème sombre de l'interface

### Performance

- Scan WiFi non-bloquant
- Utilisation du pool JSON pour éviter les fuites mémoire
- Timeout de connexion de 15 secondes
- Mise à jour automatique du statut après connexion

## Dépannage

### Problèmes courants

1. **Scan ne trouve aucun réseau**
   - Vérifier que l'ESP32 est en mode STA ou AP_STA
   - Attendre quelques secondes et réessayer

2. **Connexion échoue**
   - Vérifier le SSID et le mot de passe
   - S'assurer que le réseau est à portée
   - Vérifier la compatibilité de sécurité (WPA2/WPA3)

3. **Réseaux sauvegardés ne s'affichent pas**
   - Vérifier l'espace NVS disponible
   - Redémarrer l'ESP32 si nécessaire

### Logs de débogage

Les opérations WiFi sont loggées dans la console série :
```
[WiFi] Scanner les réseaux...
[WiFi] X réseaux trouvés
[WiFi] Tentative connexion à SSID...
[WiFi] Connecté avec succès
```

## Évolutions futures

### Améliorations possibles

- Chiffrement des mots de passe en NVS
- Support des réseaux cachés
- Historique des connexions
- Export/import de la configuration WiFi
- Support WPS (si matériel compatible)

---

**Note** : Ce gestionnaire WiFi est conçu pour être léger, flexible et intégré de manière transparente dans l'écosystème FFP3 existant.
