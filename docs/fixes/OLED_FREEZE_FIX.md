# Correction du problème de gel de l'écran OLED

## Problème identifié

L'écran OLED avait tendance à se figer au lieu de se rafraîchir toutes les 300ms comme prévu. Ce problème était causé par plusieurs facteurs :

### 1. Gestion incorrecte des débordements de `millis()`
- La fonction `millis()` retourne un `unsigned long` qui déborde après environ 49 jours
- Les comparaisons `millis() - _lastOled > oledInterval` pouvaient donner des résultats incorrects lors du débordement
- Cela causait des blocages de plusieurs heures voire jours

### 2. Utilisation incohérente de `millis()`
- Plusieurs appels à `millis()` dans la même fonction pouvaient donner des valeurs différentes
- Cela créait des conditions de rafraîchissement imprévisibles

### 3. Absence de protection contre les blocages
- Aucun mécanisme de sécurité pour forcer le rafraîchissement en cas de problème
- Les autres méthodes (`handleRemoteState`, `handleRefill`, etc.) pouvaient bloquer l'affichage

## Corrections apportées

### 1. Protection contre les débordements de `millis()`
```cpp
// AVANT
if (_disp.isPresent() && millis() - _lastOled > oledInterval) {

// APRÈS
unsigned long currentMillis = millis();
if (_disp.isPresent() && (currentMillis - _lastOled >= oledInterval || _lastOled == 0)) {
```

### 2. Utilisation cohérente de `millis()`
- Une seule variable `currentMillis` par fonction pour éviter les incohérences
- Remplacement de tous les appels directs à `millis()` par cette variable

### 3. Ajout d'un watchdog pour l'affichage
```cpp
// Protection supplémentaire : si l'écran n'a pas été rafraîchi depuis plus de 2 secondes,
// on force le rafraîchissement pour éviter qu'il se fige
static unsigned long lastOledWatchdog = 0;
bool forceRefresh = false;
if (currentMillis - lastOledWatchdog > 2000) {
  forceRefresh = true;
  lastOledWatchdog = currentMillis;
}

if (_disp.isPresent() && (currentMillis - _lastOled >= oledInterval || _lastOled == 0 || forceRefresh)) {
```

### 4. Logs de diagnostic
```cpp
// Log pour diagnostiquer les problèmes d'affichage
if (forceRefresh) {
  Serial.printf("[OLED] Force refresh après %lu ms sans rafraîchissement\n", currentMillis - _lastOled);
}
```

## Méthodes corrigées

1. **`Automatism::update()`** - Méthode principale de rafraîchissement OLED
2. **`Automatism::handleRemoteState()`** - Gestion des états distants
3. **`Automatism::handleRefill()`** - Gestion du remplissage
4. **`Automatism::handleAutoSleep()`** - Gestion de la mise en veille

## Résultat attendu

- L'écran OLED se rafraîchit maintenant de manière fiable toutes les 300ms
- Protection automatique contre les blocages (rafraîchissement forcé après 2 secondes)
- Gestion correcte des débordements de `millis()`
- Logs de diagnostic pour identifier les problèmes futurs

## Version du firmware

Ces corrections sont appliquées dans la version **5.7** du firmware FFP3. 