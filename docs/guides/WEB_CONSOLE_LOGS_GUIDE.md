# Système de Logs Web pour la Console du Navigateur

## 🎯 Objectif

Ce document décrit le système de logs avancé implémenté dans l'interface web du dashboard FFP3, permettant un diagnostic fin directement depuis la console du navigateur.

## 📊 Fonctionnalités

### **Système de Logs Structuré**
- **5 niveaux de logs** : ERROR, WARN, INFO, DEBUG, TRACE
- **Catégorisation** : ACTION, WEBSOCKET, HTTP, WIFI, INIT, etc.
- **Horodatage** : Timestamp précis pour chaque log
- **Données contextuelles** : Objets JSON pour les détails techniques
- **Emojis visuels** : Identification rapide des types de logs

### **Panneau de Logs Intégré**
- **Affichage en temps réel** dans l'interface web
- **Filtrage par niveau** : Sélection du niveau de détail souhaité
- **Historique** : Conservation des 1000 derniers logs
- **Export** : Téléchargement des logs en fichier texte
- **Effacement** : Bouton pour vider l'historique

## 🔧 Utilisation

### **Activation du Panneau**
1. Aller dans la section "📊 Logs de Debug"
2. Cliquer sur "Toggle Logs" pour activer l'affichage
3. Sélectionner le niveau de log souhaité (ERROR à TRACE)
4. Les logs s'affichent en temps réel

### **Contrôles Disponibles**
- **Toggle Logs** : Activer/désactiver l'affichage
- **Clear** : Effacer l'historique des logs
- **Niveau** : Filtrer par niveau (ERROR, WARN, INFO, DEBUG, TRACE)
- **Export** : Télécharger les logs au format texte

## 📝 Types de Logs

### **ACTION** - Actions utilisateur
```
[14:30:25] INFO 🔍 [ACTION] Démarrage action: Nourrir Petits
[14:30:25] DEBUG [HTTP] Envoi requête HTTP pour action feedSmall
[14:30:26] INFO [HTTP] Action feedSmall: FEED_SMALL OK
```

### **WEBSOCKET** - Communication temps réel
```
[14:30:25] INFO [WEBSOCKET] Tentative de connexion WebSocket sur ws://192.168.1.100:81/ws (port 81)
[14:30:26] INFO [WEBSOCKET] WebSocket connecté sur le port 81
[14:30:26] DEBUG [WEBSOCKET] Message WebSocket reçu: {"type":"sensor_update",...}
```

### **HTTP** - Requêtes réseau
```
[14:30:25] DEBUG [HTTP] Envoi requête HTTP pour action feedSmall
[14:30:26] INFO [HTTP] Action feedSmall: FEED_SMALL OK {"cmd":"feedSmall","responseTime":1250}
```

### **WIFI** - Gestion réseau
```
[14:30:25] INFO [WIFI] Tentative de connexion WebSocket sur ws://192.168.1.100:81/ws (port 81)
[14:30:26] INFO [WIFI] WebSocket connecté sur le port 81
```

### **INIT** - Initialisation
```
[14:30:25] INFO [INIT] Dashboard consolidé initialisé
[14:30:26] INFO [INIT] Panneau de logs activé
```

## 🎨 Interface Visuelle

### **Couleurs par Niveau**
- **❌ ERROR** : Rouge (`--danger`)
- **⚠️ WARN** : Orange (`--warning`)
- **ℹ️ INFO** : Bleu (`--info`)
- **🔍 DEBUG** : Vert (`--primary`)
- **🔬 TRACE** : Gris (`--muted`)

### **Structure d'Affichage**
```
[14:30:25] INFO 🔍 [ACTION] Démarrage action: Nourrir Petits {"cmd":"feedSmall"}
```

- **Timestamp** : `[14:30:25]`
- **Niveau** : `INFO`
- **Emoji** : `🔍`
- **Catégorie** : `[ACTION]`
- **Message** : `Démarrage action: Nourrir Petits`
- **Données** : `{"cmd":"feedSmall"}`

## 🔍 Exemples de Diagnostic

### **Problème de Connexion WebSocket**
```
[14:30:25] INFO [WEBSOCKET] Tentative de connexion WebSocket sur ws://192.168.1.100:81/ws (port 81)
[14:30:35] WARN [WEBSOCKET] Timeout de connexion WebSocket sur le port 81 (10s)
[14:30:35] INFO [WEBSOCKET] Tentative de connexion WebSocket sur ws://192.168.1.100:80/ws (port 80)
[14:30:36] INFO [WEBSOCKET] WebSocket connecté sur le port 80
```

### **Action Utilisateur Complète**
```
[14:30:25] INFO [ACTION] Démarrage action: Nourrir Petits
[14:30:25] DEBUG [HTTP] Envoi requête HTTP pour action feedSmall
[14:30:26] INFO [HTTP] Action feedSmall: FEED_SMALL OK
[14:30:26] INFO [WEBSOCKET] Action feedSmall confirmée via WebSocket après 1250ms
```

### **Changement de Réseau WiFi**
```
[14:30:25] INFO [WIFI] Changement de réseau WiFi vers: NouveauReseau
[14:30:25] INFO [WEBSOCKET] WebSocket fermé suite à un changement de réseau (code: 1000)
[14:30:40] INFO [WIFI] Tentative de reconnexion après changement WiFi...
[14:30:41] INFO [WIFI] Nouvelle IP ESP32 détectée: 192.168.1.101
[14:30:42] INFO [WEBSOCKET] WebSocket connecté sur le port 81
```

## 📊 Avantages

### **Pour le Développement**
- **Débogage facilité** : Logs structurés avec contexte
- **Traçabilité complète** : Suivi de toutes les opérations
- **Diagnostic en temps réel** : Pas besoin de console série

### **Pour la Maintenance**
- **Identification rapide** : Emojis et couleurs pour identification
- **Export des logs** : Sauvegarde pour analyse ultérieure
- **Filtrage intelligent** : Focus sur les niveaux pertinents

### **Pour l'Utilisateur**
- **Transparence** : Visibilité sur le fonctionnement
- **Diagnostic autonome** : Résolution de problèmes sans intervention
- **Interface intégrée** : Pas besoin d'outils externes

## 🚀 Configuration Avancée

### **Modification du Niveau de Log**
```javascript
// Changer le niveau de log par défaut
LogConfig.currentLevel = 2; // INFO

// Désactiver les logs
LogConfig.enabled = false;
```

### **Ajout de Logs Personnalisés**
```javascript
// Log d'information
logInfo('Message d\'information', { data: 'contexte' }, 'CATEGORIE');

// Log d'erreur
logError('Erreur détectée', error, 'ERROR');

// Log de debug
logDebug('Détails techniques', { param1: 'valeur1' }, 'DEBUG');
```

### **Écoute des Logs**
```javascript
// Ajouter un listener pour les nouveaux logs
window.logListeners.push(function(logEntry) {
  console.log('Nouveau log:', logEntry);
});
```

## 📈 Métriques et Performance

### **Limites**
- **Historique** : 1000 logs maximum
- **Affichage** : 100 derniers logs visibles
- **Export** : Tous les logs de l'historique

### **Optimisations**
- **Filtrage côté client** : Pas de surcharge serveur
- **Scroll automatique** : Focus sur les logs récents
- **Gestion mémoire** : Rotation automatique de l'historique

## 🔄 Maintenance

### **Nettoyage Régulier**
- Les logs sont automatiquement limités à 1000 entrées
- L'export permet de sauvegarder l'historique avant effacement
- Le filtrage par niveau réduit l'encombrement visuel

### **Diagnostic des Problèmes**
1. **Activer le panneau de logs**
2. **Sélectionner le niveau DEBUG ou TRACE**
3. **Reproduire le problème**
4. **Exporter les logs pour analyse**
5. **Identifier les patterns d'erreur**

Le système de logs web offre une visibilité complète sur le fonctionnement du dashboard, facilitant le diagnostic et l'optimisation du système FFP3.
