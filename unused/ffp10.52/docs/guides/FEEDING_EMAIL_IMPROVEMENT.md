# Amélioration des mails de nourrissage - Ajout du temps effectif

## Objectif
Ajouter le temps de nourrissage effectif dans tous les mails de nourrissage (automatique et manuel) pour une meilleure traçabilité.

## Modifications apportées

### 1. Nouvelle fonction helper
- **Fichier**: `src/automatism.cpp`
- **Fonction**: `createFeedingMessage(const char* type, uint16_t bigDur, uint16_t smallDur)`
- **Rôle**: Génère un message formaté avec les détails du temps de nourrissage

### 2. Getters ajoutés
- **Fichier**: `include/automatism.h`
- **Fonctions**: 
  - `getFeedBigDur()` - Retourne la durée de nourrissage des gros poissons
  - `getFeedSmallDur()` - Retourne la durée de nourrissage des petits poissons

### 3. Mise à jour des mails automatiques
- **Fichier**: `src/automatism.cpp` - Fonction `handleFeeding()`
- **Types de nourrissage**:
  - Bouffe matin
  - Bouffe midi  
  - Bouffe soir

### 4. Mise à jour des mails manuels
- **Fichiers**: 
  - `src/automatism.cpp` - Fonction `handleRemoteState()`
  - `src/web_server.cpp` - Endpoints `/action`
- **Types de nourrissage**:
  - Bouffe manuelle - Petits poissons
  - Bouffe manuelle - Gros poissons

## Format du nouveau message

### Avant
```
Distribution effectuée
```

### Après
```
Bouffe matin - Distribution effectuée

Temps de nourrissage effectif:
- Gros poissons: 10 secondes
- Petits poissons: 10 secondes
- Durée totale: 15 secondes
```

## Calcul de la durée totale
La durée totale est calculée comme suit :
```
Durée totale = max(durée_gros, durée_petits) + max(durée_gros, durée_petits)/2
```

Cette formule prend en compte :
- La durée maximale entre les deux types de nourrissage
- Une phase supplémentaire de 50% pour le retour à la position initiale

## Exemples de messages

### Nourrissage automatique
```
Bouffe matin - Distribution effectuée

Temps de nourrissage effectif:
- Gros poissons: 10 secondes
- Petits poissons: 10 secondes
- Durée totale: 15 secondes
```

### Nourrissage manuel
```
Bouffe manuelle - Petits poissons - Distribution effectuée

Temps de nourrissage effectif:
- Gros poissons: 12 secondes
- Petits poissons: 8 secondes
- Durée totale: 18 secondes
```

## Compilation
Le code compile sans erreur sur les deux environnements :
- `esp32dev` ✅
- `esp32-s3-devkitc-1-n16r8v` ✅

## Test
Un script de test `test_feeding_message.py` a été créé pour valider le format des messages.

## Impact
- **Amélioration de la traçabilité** : L'utilisateur connaît maintenant exactement combien de temps a duré chaque nourrissage
- **Transparence** : Les durées effectives sont affichées pour chaque type de poisson
- **Cohérence** : Tous les mails de nourrissage (auto et manuel) utilisent le même format 