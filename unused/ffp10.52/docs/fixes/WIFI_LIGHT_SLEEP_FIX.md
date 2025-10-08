# Correction de la reconnexion WiFi après Light Sleep

## Problème initial
La fonction `WiFi.reconnect()` ne fonctionnait pas correctement après un light sleep sur l'ESP32-S3, causant des problèmes de reconnexion WiFi.

## Solution implémentée

### Modifications apportées

#### 1. Ajout de variables dans `PowerManager` (`include/power.h`)
```cpp
// Identifiants WiFi sauvegardés pour light sleep
String _lastSSID;
String _lastPassword;
bool _hasSavedCredentials;
```

#### 2. Nouvelles méthodes ajoutées (`src/power.cpp`)

**`saveCurrentWifiCredentials()`**
- Sauvegarde le SSID et le mot de passe WiFi actuels avant le light sleep
- Utilise `WiFi.SSID()` et `WiFi.psk()` pour récupérer les identifiants
- Vérifie que la connexion WiFi est active avant la sauvegarde

**`reconnectWithSavedCredentials()`**
- Tente de se reconnecter avec les identifiants sauvegardés
- Utilise `WiFi.begin()` au lieu de `WiFi.reconnect()`
- Timeout de 10 secondes avec logs détaillés
- Retourne `true` si la reconnexion réussit, `false` sinon

#### 3. Modification de `goToLightSleep()`
- **Avant le sleep** : Appel de `saveCurrentWifiCredentials()` pour sauvegarder les identifiants
- **Après le sleep** : Remplacement de `WiFi.reconnect()` par `reconnectWithSavedCredentials()`
- **Fallback** : Si la reconnexion échoue, un message indique que `wifi.loop()` prendra le relais

### Flux de fonctionnement

1. **Avant le light sleep** :
   - Sauvegarde de l'heure
   - Sauvegarde des identifiants WiFi actuels
   - Déconnexion WiFi
   - Mise en veille

2. **Après le light sleep** :
   - Ajustement de l'horloge
   - Tentative de reconnexion avec les identifiants sauvegardés
   - Si échec : `wifi.loop()` prend le relais pour les tentatives périodiques

### Avantages de cette approche

- **Fiabilité** : Utilise `WiFi.begin()` qui est plus fiable que `WiFi.reconnect()`
- **Transparence** : Logs détaillés pour le debugging
- **Fallback** : Si la reconnexion échoue, le système existant (`wifi.loop()`) prend le relais
- **Compatibilité** : Ne modifie pas le comportement existant pour les autres modes de veille

### Tests recommandés

1. Vérifier que les identifiants sont correctement sauvegardés avant le sleep
2. Tester la reconnexion après différents durées de light sleep
3. Vérifier que le fallback vers `wifi.loop()` fonctionne en cas d'échec
4. Tester avec différents réseaux WiFi (avec/sans mot de passe)

### Logs de debug

Les logs suivants sont ajoutés pour faciliter le debugging :
- `[Power] Identifiants WiFi sauvegardés: SSID=xxx`
- `[Power] Tentative de reconnexion WiFi avec SSID: xxx`
- `[Power] Reconnexion WiFi réussie à xxx (IP)`
- `[Power] Échec de reconnexion WiFi à xxx (timeout après X ms)`
- `[Power] Échec reconnexion avec identifiants sauvegardés - laissez wifi.loop() prendre le relais` 