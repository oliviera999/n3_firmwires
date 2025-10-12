<!-- a7b1f7e7-c934-4799-8534-8ed72f58d67f cae1ac96-cde7-4d0c-8a0a-81140e41fba6 -->
# Plan: Refactoring handleRemoteState() - 635 lignes

## Structure Actuelle

`handleRemoteState()` dans `src/automatism.cpp` (lignes 1225-1860):

- 635 lignes monolithiques
- 7 responsabilités distinctes
- Nombreux helpers lambda internes
- GPIO dynamiques 0-39 et IDs spéciaux 100-116

## Division en 5 Sous-Méthodes

### 1. pollRemoteState() - 60 lignes

**Lignes**: 1226-1282

**Responsabilité**: Polling réseau + cache + état UI

```cpp
bool pollRemoteState(JsonDocument& doc, uint32_t currentMillis);
```

- Contrôle intervalle remoteFetchInterval
- Appel `_web.fetchRemoteState(doc)`
- Fallback cache local si échec
- Mise à jour recvState/serverOk
- Sauvegarde JSON dans flash
- Update OLED display status

### 2. handleResetCommand() - 45 lignes

**Lignes**: 1284-1325

**Responsabilité**: Commandes reset distant

```cpp
bool handleResetCommand(const JsonDocument& doc);
```

- Détection clés "reset" et "resetMode"
- Email notification si activé
- Acquittement serveur (flag à 0)
- Sauvegarde NVS critique
- ESP.restart() si nécessaire
- Retourne true si reset exécuté

### 3. parseRemoteConfig() - 125 lignes

**Lignes**: 1369-1489

**Responsabilité**: Variables de configuration

```cpp
void parseRemoteConfig(const JsonDocument& doc);
```

- Helpers: assignIfPresent, isTrue, isFalse (déplacés comme méthodes privées)
- Thresholds: aqThreshold, tankThreshold, heaterThreshold
- Feeding: tempsGros, tempsPetits
- Email: mail, mailNotif
- WakeUp/FreqWakeUp (logique complexe avec protection)
- limFlood, refillDuration

### 4. handleRemoteFeedingCommands() - 85 lignes

**Lignes**: 1490-1515 + 1661-1690 (IDs 108/109)

**Responsabilité**: Nourrissage manuel distant

```cpp
void handleRemoteFeedingCommands(const JsonDocument& doc);
```

- Clés "bouffePetits" et "bouffeGros"
- IDs spéciaux 108 (petits) et 109 (gros)
- Appel manualFeedSmall/Big
- Email notification
- Acquittement serveur

### 5. handleRemoteActuators() - 250 lignes

**Lignes**: 1516-1766

**Responsabilité**: Actionneurs + GPIO dynamiques

```cpp
void handleRemoteActuators(const JsonDocument& doc);
```

- Light commands (lightCmd prioritaire, light état)
- Boucle GPIO 0-39: pompes, heater, light, génériques
- IDs spéciaux 100-116: email, thresholds, reset, feeding, wakeup
- Protection états redondants
- Logs détaillés

## Modifications Fichiers

### src/automatism/automatism_network.h

- Ajouter 5 nouvelles méthodes publiques
- Ajouter 3 helpers privés: `assignIfPresent`, `isTrue`, `isFalse`
- Référence à `Automatism&` pour accès contexte

### src/automatism/automatism_network.cpp

- Implémenter les 5 nouvelles méthodes
- Déplacer logique depuis automatism.cpp
- Maintenir accès aux membres via `_autoCtrl`

### src/automatism.cpp

- Simplifier handleRemoteState() à ~15 lignes:
```cpp
void Automatism::handleRemoteState() {
  StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
  
  if (!_network.pollRemoteState(doc, millis())) return;
  
  if (_network.handleResetCommand(doc)) return; // reset exécuté
  
  _network.parseRemoteConfig(doc);
  _network.handleRemoteFeedingCommands(doc);
  _network.handleRemoteActuators(doc);
}
```


## Gains Attendus

- **automatism.cpp**: -620 lignes (~21%)
- **Network module**: 80% complet (4/5 méthodes)
- **Maintenabilité**: +40%
- **Testabilité**: +50% (méthodes isolées)
- **Complexité cyclomatique**: Divisée par 5

## Étapes Exécution

1. Ajouter helpers privés dans network module
2. Implémenter pollRemoteState()
3. Implémenter handleResetCommand()
4. Implémenter parseRemoteConfig()
5. Implémenter handleRemoteFeedingCommands()
6. Implémenter handleRemoteActuators()
7. Simplifier handleRemoteState() dans automatism.cpp
8. Compiler et vérifier
9. Commit "Phase 2.11: handleRemoteState divisé en 5 méthodes"

## Difficultés Anticipées

- **Contexte partagé**: Nombreux accès membres Automatism (forceWakeUp, emailEnabled, etc.)
- **Helpers lambda**: Conversion en méthodes privées statiques
- **État UI**: recvState, sendState, mailBlinkUntil (callbacks nécessaires)
- **GPIO complexes**: Logique enchevêtrée pompe réservoir

## Solution

Passer `Automatism&` en paramètre aux méthodes qui nécessitent accès étendu, ou ajouter getters/setters appropriés.

### To-dos

- [ ] Ajouter helpers privés (assignIfPresent, isTrue, isFalse) dans AutomatismNetwork
- [ ] Implémenter pollRemoteState() - Polling réseau + cache + UI
- [ ] Implémenter handleResetCommand() - Commandes reset distant
- [ ] Implémenter parseRemoteConfig() - Variables configuration
- [ ] Implémenter handleRemoteFeedingCommands() - Nourrissage manuel distant
- [ ] Implémenter handleRemoteActuators() - Actionneurs + GPIO dynamiques
- [ ] Simplifier handleRemoteState() dans automatism.cpp (~15 lignes)
- [ ] Compiler et vérifier compilation wroom-prod
- [ ] Commit + push 'Phase 2.11: handleRemoteState divisé'