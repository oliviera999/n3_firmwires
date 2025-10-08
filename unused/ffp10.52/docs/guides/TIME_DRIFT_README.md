# Système de Surveillance de Dérive Temporelle

## Introduction

Le système de surveillance de dérive temporelle (`TimeDriftMonitor`) est un composant intégré qui surveille en permanence la précision de l'horloge de l'ESP32 en la comparant avec le temps NTP (Network Time Protocol).

## Fonctionnalités Principales

### 🔍 Surveillance Continue
- Calcul automatique de la dérive en PPM (parties par million)
- Comparaison régulière avec le temps NTP
- Détection automatique des dérives élevées

### 📊 Rapports et Alertes
- Affichage périodique sur le moniteur série
- Inclusion dans les emails de test et digests
- Alertes automatiques en cas de dérive excessive

### 💾 Persistance des Données
- Sauvegarde automatique en NVS (Non-Volatile Storage)
- Restauration des données au redémarrage
- Configuration persistante

## Installation et Configuration

### 1. Fichiers Ajoutés
```
include/time_drift_monitor.h    # Header de la classe
src/time_drift_monitor.cpp      # Implémentation
docs/guides/TIME_DRIFT_MONITORING.md  # Documentation détaillée
tools/test_time_drift.py        # Script de test
tools/configure_time_drift.py   # Script de configuration
```

### 2. Intégration Automatique
Le système est automatiquement intégré dans `app.cpp` :
- Initialisation dans `setup()`
- Mise à jour dans `loop()`
- Affichage dans les tâches périodiques

### 3. Configuration NVS
Le namespace `timeDrift` est automatiquement créé dans NVS.

## Utilisation

### Surveillance Automatique
Aucune action requise - le système fonctionne automatiquement :
- Synchronisation NTP toutes les heures
- Calcul de dérive à chaque sync
- Affichage toutes les 30 minutes
- Sauvegarde automatique

### Affichage sur Moniteur Série
```
=== INFORMATIONS DE DÉRIVE TEMPORELLE ===
Dérive: 45.23 PPM (2.15 secondes)
Dernière sync: 14:30:25 15/01/2024
Statut: Dérive: 45.23 PPM (✓ Normale)
Seuil d'alerte: 100.00 PPM
=========================================
```

### Emails
Les informations de dérive sont incluses dans :
- **Email de test au démarrage** : Rapport complet
- **Digest périodique** : Résumé des informations

## Configuration Avancée

### Paramètres Modifiables
```cpp
// Dans le code source
timeDriftMonitor.setSyncInterval(1800000);  // 30 minutes
timeDriftMonitor.setDriftThreshold(50.0f);  // 50 PPM
```

### Scripts de Configuration
```bash
# Configuration interactive
python tools/configure_time_drift.py

# Test du système
python tools/test_time_drift.py
```

## Interprétation des Résultats

### Dérive Normale
- **< 50 PPM** : Excellente précision
- **50-100 PPM** : Bonne précision
- **100-200 PPM** : Précision acceptable

### Dérive Élevée
- **> 200 PPM** : Précision insuffisante
- **> 500 PPM** : Dérive importante

### Facteurs Influençant la Dérive
1. **Température** : Variations de température
2. **Alimentation** : Stabilité de la tension
3. **Vieillissement** : Dérive du cristal
4. **Interférences** : Signaux électromagnétiques

## Dépannage

### Problèmes Courants

#### Pas de Calcul de Dérive
- Vérifier la connexion WiFi
- Vérifier la synchronisation NTP
- Consulter les logs série

#### Dérive Très Élevée
- Vérifier la température ambiante
- Vérifier la stabilité de l'alimentation
- Considérer un redémarrage

#### Sync NTP Échoue
- Vérifier la connectivité réseau
- Vérifier la configuration NTP
- Consulter les logs de diagnostic

### Logs de Diagnostic
```
[TimeDrift] Synchronisation NTP en cours...
[TimeDrift] Sync NTP réussie: 14:30:25 15/01/2024
[TimeDrift] Calcul de dérive:
  - Temps local écoulé: 3600000 ms (3600 s)
  - Dérive: 2.15 secondes (45.23 PPM)
[TimeDrift] Données sauvegardées en NVS
```

## Maintenance

### Actions Recommandées
1. **Surveillance régulière** : Vérifier les rapports périodiques
2. **Dérive normale** : Aucune action requise
3. **Dérive élevée** : 
   - Vérifier l'environnement
   - Considérer un redémarrage
   - Augmenter la fréquence de sync

### Surveillance à Long Terme
- Tendance de dérive sur plusieurs jours
- Corrélation avec les conditions environnementales
- Détection de problèmes matériels

## Intégration avec l'Application

### Variables NVS
- `timeDrift.driftPPM` : Dérive en PPM
- `timeDrift.driftSeconds` : Dérive en secondes
- `timeDrift.lastSyncTime` : Timestamp de dernière sync
- `timeDrift.driftCalculated` : Flag de calcul effectué

### Callbacks et Événements
- Synchronisation NTP réussie
- Dérive élevée détectée
- Sauvegarde des données

## Support et Documentation

### Documentation Complète
- `docs/guides/TIME_DRIFT_MONITORING.md` : Guide détaillé
- `tools/test_time_drift.py` : Script de test
- `tools/configure_time_drift.py` : Script de configuration

### Exemples de Configuration
- `tools/time_drift_config_example.json` : Configuration d'exemple

### Logs et Diagnostic
- Moniteur série : Informations en temps réel
- Emails : Rapports périodiques
- NVS : Persistance des données

## Conclusion

Le système de surveillance de dérive temporelle fournit une surveillance continue et automatique de la précision de l'horloge de l'ESP32. Il est entièrement intégré dans l'application et ne nécessite aucune configuration manuelle pour fonctionner.

Pour plus d'informations, consultez la documentation détaillée dans `docs/guides/TIME_DRIFT_MONITORING.md`.
