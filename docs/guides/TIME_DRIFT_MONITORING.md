# Surveillance de la Dérive Temporelle

## Vue d'ensemble

Le système de surveillance de la dérive temporelle (`TimeDriftMonitor`) surveille en permanence la précision de l'horloge de l'ESP32 en la comparant régulièrement avec le temps NTP (Network Time Protocol).

## Fonctionnalités

### 1. Calcul de la Dérive
- **Dérive en PPM** : Mesure en parties par million (PPM) de la dérive de l'horloge locale
- **Dérive en secondes** : Dérive absolue en secondes par rapport au temps NTP
- **Seuil d'alerte** : Alerte automatique si la dérive dépasse un seuil configurable

### 2. Synchronisation NTP
- Synchronisation automatique avec le serveur NTP configuré
- Intervalle de synchronisation configurable (par défaut : 1 heure)
- Gestion des échecs de connexion WiFi

### 3. Persistance des Données
- Sauvegarde automatique des données de dérive en NVS (Non-Volatile Storage)
- Restauration des données au redémarrage
- Namespace NVS : `timeDrift`

### 4. Rapports et Alertes
- Affichage périodique sur le moniteur série (toutes les 30 minutes)
- Inclusion dans les emails de test au démarrage
- Inclusion dans les digests périodiques d'événements
- Alertes automatiques en cas de dérive élevée

## Configuration

### Paramètres par Défaut
```cpp
- Intervalle de sync NTP : 1 heure (3600000 ms)
- Seuil d'alerte : 100 PPM (0.1%)
- Intervalle d'affichage : 30 minutes
```

### Personnalisation
```cpp
timeDriftMonitor.setSyncInterval(1800000);  // 30 minutes
timeDriftMonitor.setDriftThreshold(50.0f);  // 50 PPM
```

## Utilisation

### Initialisation
Le moniteur est automatiquement initialisé dans `setup()` :
```cpp
timeDriftMonitor.begin();
```

### Mise à Jour
La mise à jour est automatique dans `loop()` :
```cpp
timeDriftMonitor.update();
```

### Accès aux Données
```cpp
float driftPPM = timeDriftMonitor.getDriftPPM();
float driftSeconds = timeDriftMonitor.getDriftSeconds();
String status = timeDriftMonitor.getDriftStatusString();
```

## Interprétation des Résultats

### Dérive Normale
- **< 50 PPM** : Excellente précision
- **50-100 PPM** : Bonne précision
- **100-200 PPM** : Précision acceptable

### Dérive Élevée
- **> 200 PPM** : Précision insuffisante
- **> 500 PPM** : Dérive importante, redémarrage recommandé

### Facteurs Influençant la Dérive
1. **Température** : Les variations de température affectent la fréquence du cristal
2. **Tension d'alimentation** : Les variations de tension influencent la précision
3. **Vieillissement** : Le cristal peut dériver avec le temps
4. **Interférences** : Les signaux électromagnétiques peuvent perturber l'horloge

## Surveillance et Maintenance

### Moniteur Série
Les informations de dérive sont affichées toutes les 30 minutes :
```
=== INFORMATIONS DE DÉRIVE TEMPORELLE ===
Dérive: 45.23 PPM (2.15 secondes)
Dernière sync: 14:30:25 15/01/2024
Statut: Dérive: 45.23 PPM (✓ Normale)
Seuil d'alerte: 100.00 PPM
=========================================
```

### Emails
Les rapports de dérive sont inclus dans :
- **Email de test au démarrage** : Rapport complet
- **Digest périodique** : Résumé des informations

### Actions Recommandées
1. **Dérive normale** : Aucune action requise
2. **Dérive élevée** : 
   - Vérifier la température ambiante
   - Vérifier la stabilité de l'alimentation
   - Considérer un redémarrage
   - Augmenter la fréquence de synchronisation NTP

## Dépannage

### Problèmes Courants
1. **Pas de calcul de dérive** : Vérifier la connexion WiFi et la synchronisation NTP
2. **Dérive très élevée** : Problème matériel possible, vérifier l'alimentation
3. **Sync NTP échoue** : Vérifier la connectivité réseau et la configuration NTP

### Logs de Diagnostic
Les logs de diagnostic sont disponibles dans le moniteur série :
```
[TimeDrift] Synchronisation NTP en cours...
[TimeDrift] Sync NTP réussie: 14:30:25 15/01/2024
[TimeDrift] Calcul de dérive:
  - Temps local écoulé: 3600000 ms (3600 s)
  - Dérive: 2.15 secondes (45.23 PPM)
[TimeDrift] Données sauvegardées en NVS
```

## Intégration

Le système est entièrement intégré dans l'application principale et ne nécessite aucune configuration supplémentaire. Les données sont automatiquement sauvegardées et restaurées au redémarrage.
