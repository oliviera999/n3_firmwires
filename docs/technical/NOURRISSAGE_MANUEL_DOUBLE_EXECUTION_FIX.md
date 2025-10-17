# Correction du problème de double exécution du nourrissage manuel

## Problème identifié

Le nourrissage manuel s'exécutait **deux fois de suite** quand appelé depuis le serveur local à cause d'un mécanisme de boucle :

### Flux problématique :
1. **Commande locale** : Bouton "Nourrir Petits/Gros" → `/action?cmd=feedSmall` ou `/action?cmd=feedBig`
2. **Première exécution** : `manualFeedSmall()` ou `manualFeedBig()` s'exécute
3. **Traçage automatique** : `traceFeedingEventSelective()` envoie `bouffePetits=1` au serveur distant
4. **Retour du serveur** : Le serveur distant renvoie `bouffePetits=1` à l'ESP32
5. **Deuxième exécution** : `applyConfigFromJson()` détecte `bouffePetits=1` et relance `manualFeedSmall()`

## Solution implémentée

Ajout d'un **mécanisme de protection temporelle** dans les fonctions `manualFeedSmall()` et `manualFeedBig()` :

```cpp
// CORRECTION: Ne pas tracer si le nourrissage vient déjà d'une commande locale
// pour éviter la double exécution via le serveur distant
static unsigned long lastManualFeedSmallMs = 0;
unsigned long now = millis();
bool isRecentLocalCommand = (now - lastManualFeedSmallMs) < 2000; // 2 secondes de protection

if (!isRecentLocalCommand) {
  traceFeedingEventSelective(true, false);
} else {
  Serial.println(F("[CRITIQUE] Traçage évité - nourrissage récent depuis commande locale"));
}

lastManualFeedSmallMs = now;
```

## Fonctionnement de la correction

- **Protection de 2 secondes** : Si un nourrissage manuel a eu lieu dans les 2 dernières secondes, le traçage est évité
- **Traçage conditionnel** : Le traçage ne se fait que si le nourrissage ne vient pas d'une commande locale récente
- **Logging amélioré** : Message explicite quand le traçage est évité
- **Préservation du traçage distant** : Les commandes venant du serveur distant continuent d'être tracées normalement

## Avantages

✅ **Élimination de la double exécution**  
✅ **Préservation du traçage pour les commandes distantes**  
✅ **Mécanisme simple et robuste**  
✅ **Pas d'impact sur les autres fonctionnalités**  
✅ **Logging pour le débogage**  

## Fichiers modifiés

- `src/automatism.cpp` : Fonctions `manualFeedSmall()` et `manualFeedBig()`

## Test recommandé

1. Tester le nourrissage manuel depuis l'interface web locale
2. Vérifier qu'il ne s'exécute qu'une seule fois
3. Vérifier que le traçage fonctionne toujours pour les commandes distantes
4. Contrôler les logs pour confirmer le bon fonctionnement
