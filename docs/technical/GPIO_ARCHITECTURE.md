# Architecture GPIO - Système Unifié v11.68

## Vue d'ensemble

Le système GPIO Parsing Unifié remplace l'ancien système complexe de parsing avec multiples formats par une architecture simple et robuste basée sur une **source unique de vérité**.

### Principe Fondamental

**Source unique de vérité** : `include/gpio_mapping.h`

Tous les GPIO (physiques 0-39 + virtuels 100-116) sont définis dans une table centralisée synchronisée avec le serveur distant.

## Flux de Données

### ESP32 → Serveur Distant
1. **Action locale** → `GPIONotifier::notifyChange()`
2. **POST instantané** `/post-data` avec payload partiel
3. **BDD mise à jour** immédiatement

### Serveur Distant → ESP32  
1. **GET** `/api/outputs/state` (GPIO numériques uniquement)
2. **Parsing unifié** `GPIOParser::parseAndApply()`
3. **Application hardware** + **sauvegarde NVS** automatique

## Table de Mapping Centralisée

| GPIO | Type | Variable ESP32 | Clé NVS | Description |
|------|------|----------------|---------|-------------|
| 16   | Actuator | pump_aqua | pump_aqua | Pompe aquarium |
| 18   | Actuator | pump_tank | pump_tank | Pompe réservoir |
| 15   | Actuator | light | light | Lumière |
| 2    | Actuator | heater | heater | Chauffage |
| 108  | Actuator | feed_small | feed_small | Nourrir petits |
| 109  | Actuator | feed_big | feed_big | Nourrir gros |
| 100  | Config | emailAddress | email | Email |
| 101  | Config | emailEnabled | emailEn | Notif email |
| 102  | Config | aqThresholdCm | aqThr | Seuil aquarium |
| 103  | Config | tankThresholdCm | tankThr | Seuil réservoir |
| 104  | Config | heaterThresholdC | heatThr | Seuil chauffage |
| 105  | Config | feedMorning | feedMorn | Heure matin |
| 106  | Config | feedNoon | feedNoon | Heure midi |
| 107  | Config | feedEvening | feedEve | Heure soir |
| 110  | Actuator | reset | reset | Reset ESP32 |
| 111  | Config | feedBigDur | feedBigD | Durée gros |
| 112  | Config | feedSmallDur | feedSmD | Durée petits |
| 113  | Config | refillDurationMs | refillD | Durée remplissage |
| 114  | Config | limFlood | limFlood | Limite inondation |
| 115  | Config | forceWakeUp | wakeup | Forcer réveil |
| 116  | Config | freqWakeSec | freqWake | Fréq réveil |

## Architecture Technique

### Fichiers Principaux

#### ESP32
- `include/gpio_mapping.h` - **Source unique de vérité**
- `include/gpio_parser.h` - Interface parsing unifié
- `src/gpio_parser.cpp` - Implémentation parsing + NVS
- `include/gpio_notifier.h` - Interface POST instantané  
- `src/gpio_notifier.cpp` - Implémentation notifications
- `src/automatism.cpp` - **Simplifié** (6 étapes → 1 appel)

#### Serveur Distant
- `ffp3/src/Controller/OutputController.php` - **Format GPIO numérique uniquement**

### Simplifications Majeures

#### Avant (v11.67)
```cpp
// 6 étapes complexes dans automatism.cpp
if (_network.handleResetCommand(doc, *this)) return;
_network.parseRemoteConfig(doc, *this);
_network.handleRemoteFeedingCommands(doc, *this);
_network.handleRemoteActuators(doc, *this);
// + 250 lignes de GPIO dynamiques...
```

#### Après (v11.68)
```cpp
// 1 seule étape simple
GPIOParser::parseAndApply(doc, *this);
```

### Suppression Code Obsolète

**~630 lignes supprimées** :
- `normalizeServerKeys()` - 80 lignes
- `parseRemoteConfig()` - 50 lignes  
- `handleRemoteFeedingCommands()` - 70 lignes
- `handleRemoteActuators()` - 180 lignes
- GPIO dynamiques boucle - 250 lignes

**~350 lignes ajoutées** :
- `gpio_mapping.h` - 120 lignes
- `gpio_parser.cpp/h` - 150 lignes
- `gpio_notifier.cpp/h` - 80 lignes

**Gain net : -280 lignes + code beaucoup plus simple**

## Format de Communication

### Serveur → ESP32 (GET)
```json
{
  "16": 1,
  "18": 0, 
  "15": 1,
  "102": 25,
  "104": 18.5
}
```

### ESP32 → Serveur (POST partiel)
```
api_key=xxx&sensor=esp32&version=11.68&16=1
```

## Priorité Locale Simplifiée

### Avant (Complexe)
- Timeout variable (5 secondes)
- Vérification sync en attente
- Logique conditionnelle complexe

### Après (Simple)
- **Fenêtre fixe 1 seconde** après action locale
- Protection anti-collision POST/GET
- Logique linéaire

## Sauvegarde NVS Automatique

Chaque GPIO est automatiquement sauvé en NVS selon son type :

```cpp
switch (mapping.type) {
    case GPIOType::ACTUATOR:
    case GPIOType::CONFIG_BOOL:
        prefs.putBool(mapping.nvsKey, parseBool(value));
        break;
    case GPIOType::CONFIG_INT:
        prefs.putInt(mapping.nvsKey, parseInt(value));
        break;
    case GPIOType::CONFIG_FLOAT:
        prefs.putFloat(mapping.nvsKey, parseFloat(value));
        break;
    case GPIOType::CONFIG_STRING:
        prefs.putString(mapping.nvsKey, parseString(value));
        break;
}
```

## Interface Web (Non Impactée)

L'interface web continue de fonctionner normalement car elle lit directement la BDD MySQL. Le mapping sémantique est géré côté serveur PHP.

## Avantages du Nouveau Système

### ✅ Robustesse
- **Source unique de vérité** : pas de désynchronisation
- **Parsing simple** : GPIO numérique → variable directe
- **Sauvegarde automatique** : tous les GPIO en NVS

### ✅ Simplicité  
- **1 seul parser** au lieu de 6 étapes
- **Format unifié** : GPIO numérique uniquement
- **Code réduit** : -280 lignes net

### ✅ Performance
- **POST instantané** : changements locaux immédiats
- **Priorité locale** : fenêtre fixe 1 seconde
- **Pas de normalisation** : parsing direct

### ✅ Maintenabilité
- **Table centralisée** : ajout GPIO en 1 ligne
- **Types explicites** : validation automatique
- **Logs clairs** : traçabilité complète

## Migration et Compatibilité

### Interface Web
- ✅ **Aucun impact** - lit directement BDD
- ✅ **Mapping sémantique** conservé côté serveur PHP

### Serveur Distant  
- ✅ **Format simplifié** - GPIO numérique uniquement
- ✅ **POST partiel** déjà compatible

### ESP32
- ✅ **Parsing unifié** - remplace 6 étapes
- ✅ **NVS automatique** - tous GPIO sauvés
- ✅ **Notifications instantanées** - POST immédiat

## Tests et Validation

### Tests Automatisés
```bash
python3 test_gpio_sync.py
```

### Monitoring Obligatoire
- **90 secondes** après chaque déploiement
- **Analyse logs** : erreurs, warnings, GPIO, NVS
- **Focus stabilité** : watchdog, mémoire, WiFi/WebSocket

## Conclusion

Le système GPIO Parsing Unifié v11.68 apporte :

1. **Simplicité** : 1 parser au lieu de 6 étapes
2. **Robustesse** : source unique de vérité + sauvegarde automatique  
3. **Performance** : POST instantané + priorité locale optimisée
4. **Maintenabilité** : code réduit + architecture claire

**Résultat** : Système plus simple, plus robuste, plus performant et plus maintenable.
