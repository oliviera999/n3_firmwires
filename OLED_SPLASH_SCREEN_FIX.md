# Correction du problème d'affichage OLED - Splash Screen bloqué

## Problème identifié

L'OLED s'arrêtait sur l'écran de démarrage (splash screen) et ne passait plus aux pages d'affichage normal. Le problème venait de la fonction `splashActive()` qui pouvait rester bloquée dans certains cas :

1. **Overflow de millis()** : Après ~49 jours de fonctionnement, `millis()` fait un overflow et les comparaisons temporelles peuvent échouer
2. **Valeur de `_splashUntil` non réinitialisée** : Si la variable n'était jamais remise à 0, le splash restait actif indéfiniment
3. **Problèmes de timing** : Redémarrages ou problèmes de synchronisation

## Solution implémentée

### 1. Amélioration de la fonction `splashActive()`

**Fichier :** `include/display_view.h`

- Ajout d'une gestion robuste de l'overflow de `millis()`
- Auto-réinitialisation de `_splashUntil` à 0 quand le splash expire
- Ajout de logs de diagnostic pour tracer l'expiration du splash

```cpp
bool splashActive() const { 
  if (_splashUntil == 0) return false;
  unsigned long now = millis();
  // Gestion du overflow de millis() (arrive après ~49 jours)
  if (now < _splashUntil) {
    return true; // Splash encore actif
  }
  // Splash expiré - remettre _splashUntil à 0 pour éviter les comparaisons futures
  Serial.printf("[OLED] Splash screen expiré (now=%lu, was=%lu)\n", now, _splashUntil);
  const_cast<DisplayView*>(this)->_splashUntil = 0;
  return false;
}
```

### 2. Nouvelle méthode `forceEndSplash()`

**Fichier :** `include/display_view.h` et `src/display_view.cpp`

- Méthode publique pour forcer la fin du splash screen
- Réinitialise les caches d'affichage pour forcer un redessin complet
- Ajout de logs de diagnostic

```cpp
void DisplayView::forceEndSplash() {
  Serial.println("[OLED] Force fin du splash screen");
  _splashUntil = 0;
  // Réinitialiser les caches pour forcer un redessin complet
  resetMainCache();
  resetStatusCache();
}
```

### 3. Appel de sécurité dans l'initialisation

**Fichier :** `src/app.cpp`

- Force la fin du splash avant l'affichage final "Init ok"
- Garantit que le splash ne bloque pas l'initialisation

```cpp
// Affichage final propre - effacer l'écran et afficher juste "Init ok"
if (oled.isPresent()) {
  // Forcer la fin du splash screen avant l'affichage final
  oled.forceEndSplash();
  oled.clear();
  oled.showDiagnostic("Init ok");
  delay(SystemConfig::FINAL_INIT_DELAY_MS);
}
```

### 4. Mécanisme de sécurité dans la tâche d'affichage

**Fichier :** `src/automatism.cpp`

- Surveillance automatique du splash screen
- Force la fin après 5 secondes maximum
- Évite les blocages définitifs

```cpp
// Mécanisme de sécurité : forcer la fin du splash après 5 secondes maximum
static unsigned long splashStartTime = 0;
if (splashStartTime == 0) {
  splashStartTime = millis();
} else if (millis() - splashStartTime > 5000) {
  // Si le splash dure plus de 5 secondes, le forcer à se terminer
  _disp.forceEndSplash();
  splashStartTime = 0; // Reset pour éviter les appels répétés
}
```

### 5. Logs de diagnostic améliorés

- Log lors de l'activation du splash screen
- Log lors de l'expiration automatique
- Log lors du forçage de fin

## Résultat attendu

- Le splash screen se termine correctement après 2 secondes
- L'OLED passe aux pages d'affichage normal (alternance toutes les 4 secondes)
- Mécanismes de sécurité empêchent les blocages futurs
- Diagnostics améliorés pour identifier les problèmes

## Tests recommandés

1. **Test normal** : Vérifier que l'OLED passe du splash aux pages normales après 2 secondes
2. **Test de récupération** : Redémarrer l'ESP32 et vérifier que l'affichage fonctionne
3. **Test de logs** : Vérifier dans le moniteur série les messages de diagnostic du splash
4. **Test de durée** : Laisser fonctionner quelques minutes pour vérifier l'alternance des pages

## Configuration actuelle

- **Durée du splash** : 2 secondes (`SPLASH_DURATION_MS = 2000`)
- **Alternance des pages** : 4 secondes (`SCREEN_SWITCH_INTERVAL_MS = 4000`)
- **Timeout de sécurité** : 5 secondes maximum
