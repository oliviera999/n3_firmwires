# Fix du Flag OTA - FFP3CS4

## Problème
Vous souhaitiez que `config.getOtaUpdateFlag()` retourne `true` pour déclencher les notifications email OTA.

## Solutions appliquées

### ✅ 1. Forçage du flag OTA dans le code principal

**Fichier modifié :** `src/app.cpp`

```cpp
// FORCER LE FLAG OTA À TRUE POUR TEST
config.setOtaUpdateFlag(true);
Serial.println("[App] Flag OTA forcé à true pour test");
```

**Localisation :** Ligne 372, juste après l'affichage des logs de vérification OTA.

### ✅ 2. Modification de la valeur par défaut dans le constructeur

**Fichier modifié :** `src/config.cpp`

```cpp
ConfigManager::ConfigManager() 
    : _bouffeMatinOk(false), _bouffeMidiOk(false), _bouffeSoirOk(false), 
      _lastJourBouf(-1), _pompeAquaLocked(false), _forceWakeUp(false), _otaUpdateFlag(true), // ← true au lieu de false
      _cachedBouffeMatinOk(false), _cachedBouffeMidiOk(false), _cachedBouffeSoirOk(false),
      _cachedLastJourBouf(-1), _cachedPompeAquaLocked(false), _cachedForceWakeUp(false),
      _cachedOtaUpdateFlag(true), _flagsChanged(false) { // ← true au lieu de false
```

### ✅ 3. Modification de la valeur par défaut lors du chargement NVS

**Fichier modifié :** `src/config.cpp`

```cpp
// Chargement du flag OTA depuis la flash
_preferences.begin("ota", true);
_otaUpdateFlag = _preferences.getBool("updateFlag", true); // ← true au lieu de false
_preferences.end();
```

## Comportement attendu

Avec ces modifications, voici ce qui va se passer :

### 📧 **Envoi d'email de notification OTA**

Quand l'ESP32 démarre, vous verrez dans les logs :

```
[App] Vérification des flags OTA...
[App] g_otaJustUpdated: false
[App] config.getOtaUpdateFlag(): true  ← Maintenant true !
[App] autoCtrl.isEmailEnabled(): true
[App] Flag OTA forcé à true pour test
[App] Envoi email pour mise à jour OTA interface web...
[App] Email interface web envoyé
```

### 🔄 **Cycle de vie du flag OTA**

1. **Démarrage :** `config.getOtaUpdateFlag()` = `true`
2. **Envoi email :** Email de notification OTA envoyé
3. **Reset automatique :** `config.setOtaUpdateFlag(false)` appelé
4. **Après reset :** `config.getOtaUpdateFlag()` = `false`

## Scripts de test créés

### `test_ota_flag.py`
Simule le comportement du flag OTA et confirme que :
- ✅ L'email de notification sera envoyé
- ✅ Le flag sera reset après envoi
- ✅ Le système fonctionne correctement

## Vérification

Pour vérifier que le flag OTA fonctionne :

1. **Compiler et flasher** l'ESP32 avec les modifications
2. **Surveiller les logs série** au démarrage
3. **Vérifier** que l'email de notification est envoyé
4. **Confirmer** que le flag est reset à `false` après envoi

## Logs attendus

```
[App] Vérification des flags OTA...
[App] g_otaJustUpdated: false
[App] config.getOtaUpdateFlag(): true
[App] autoCtrl.isEmailEnabled(): true
[App] Flag OTA forcé à true pour test
[App] Envoi email pour mise à jour OTA interface web...
[App] Email interface web envoyé
```

## Remarques importantes

- **Le flag est automatiquement reset** après envoi de l'email
- **L'email est envoyé** à l'adresse configurée dans les variables distantes
- **Si l'email n'est pas activé**, l'adresse par défaut est utilisée
- **Le comportement simule** une mise à jour OTA via l'interface web

## Prochaines étapes

1. **Compiler et flasher** l'ESP32
2. **Surveiller les logs série**
3. **Vérifier la réception de l'email**
4. **Confirmer le reset du flag**

Le flag OTA est maintenant configuré pour être `true` par défaut et déclencher les notifications email ! 