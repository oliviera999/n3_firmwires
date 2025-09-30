# Correction du problème d'envoi d'emails après mise à jour OTA

## Problème identifié

Aucun email n'était envoyé suite à une mise à jour OTA depuis le serveur ou l'interface web.

## Causes identifiées

1. **Flag OTA non chargé** : Le flag `_otaUpdateFlag` dans `ConfigManager` était initialisé à `false` dans le constructeur mais n'était jamais chargé depuis la flash au démarrage.

2. **Ordre d'exécution incorrect** : La vérification des flags OTA se faisait avant la récupération des variables distantes, ce qui pouvait causer `autoCtrl.isEmailEnabled()` à retourner `false` même si l'email était activé sur le serveur.

3. **Pas de fallback** : Si l'email n'était pas activé dans les variables distantes, aucun email n'était envoyé.

## Solutions appliquées

### 1. Modification de `src/config.cpp`

Ajout du chargement du flag OTA dans la méthode `loadBouffeFlags()` :

```cpp
void ConfigManager::loadBouffeFlags() {
  _preferences.begin("bouffe", true);
  _bouffeMatinOk = _preferences.getBool("bouffeMatinOk", false);
  _bouffeMidiOk = _preferences.getBool("bouffeMidiOk", false);
  _bouffeSoirOk = _preferences.getBool("bouffeSoirOk", false);
  _lastJourBouf = _preferences.getInt("lastJourBouf", -1);
  _pompeAquaLocked = _preferences.getBool("pompeAquaLocked", false);
  _forceWakeUp = _preferences.getBool("forceWakeUp", false);
  _preferences.end();
  
  // Chargement du flag OTA depuis la flash
  _preferences.begin("ota", true);
  _otaUpdateFlag = _preferences.getBool("updateFlag", false);
  _preferences.end();
  
  Serial.println(F("[Config] Flags de bouffe chargés depuis la flash"));
  Serial.printf("[Config] Matin: %s, Midi: %s, Soir: %s, Jour: %d, Pompe lock: %s, ForceWakeUp: %s\n",
                _bouffeMatinOk ? "OK" : "KO", 
                _bouffeMidiOk ? "OK" : "KO", 
                _bouffeSoirOk ? "OK" : "KO", 
                _lastJourBouf,
                _pompeAquaLocked ? "OUI" : "NON",
                _forceWakeUp ? "OUI" : "NON");
  Serial.printf("[Config] Flag OTA update: %s\n", _otaUpdateFlag ? "true" : "false");
}
```

### 2. Correction de l'ordre d'exécution dans `src/app.cpp`

Déplacement de la vérification OTA après la récupération des variables distantes :

```cpp
// 1) Récupération des variables distantes AVANT la vérification OTA
StaticJsonDocument<4096> tmp;
autoCtrl.fetchRemoteState(tmp);

// 2) Vérification des mises à jour OTA et envoi d'emails de notification
// (maintenant que les variables distantes sont à jour)
Serial.println("[App] Vérification des flags OTA...");
Serial.printf("[App] g_otaJustUpdated: %s\n", g_otaJustUpdated ? "true" : "false");
Serial.printf("[App] config.getOtaUpdateFlag(): %s\n", config.getOtaUpdateFlag() ? "true" : "false");
Serial.printf("[App] autoCtrl.isEmailEnabled(): %s\n", autoCtrl.isEmailEnabled() ? "true" : "false");
```

### 3. Ajout d'un système de fallback

Si l'email n'est pas activé dans les variables distantes, utilisation de l'adresse par défaut :

```cpp
// 2. Mise à jour via interface web ElegantOTA
if (config.getOtaUpdateFlag()) {
  // Vérifier si l'email est activé, sinon utiliser l'adresse par défaut
  bool emailEnabled = autoCtrl.isEmailEnabled();
  String emailAddress = emailEnabled ? autoCtrl.getEmailAddress() : String(Config::DEFAULT_MAIL_TO);
  
  if (emailEnabled) {
    Serial.println("[App] Envoi email pour mise à jour OTA interface web...");
  } else {
    Serial.println("[App] Email non activé dans les variables distantes, utilisation de l'adresse par défaut...");
  }
  
  String body = String("Mise à jour OTA effectuée avec succès.\n\n");
  body += "Détails de la mise à jour:\n";
  body += "- Méthode: Interface web ElegantOTA\n";
  body += "- Nouvelle version: " + String(Config::VERSION) + "\n";
  body += "- Compilé le: " + String(__DATE__) + " " + String(__TIME__) + "\n";
  body += "- Redémarrage automatique effectué";
  bool emailSent = mailer.sendAlert("OTA mise à jour - Interface web", body, emailAddress.c_str());
  Serial.printf("[App] Email interface web %s\n", emailSent ? "envoyé" : "échoué");
  config.setOtaUpdateFlag(false); // reset du flag
}
```

### 4. Ajout d'un endpoint de test

Nouvel endpoint `/testota` pour activer manuellement le flag OTA :

```cpp
// /testota endpoint: active manuellement le flag OTA pour les tests
_server.on("/testota", HTTP_GET, [](AsyncWebServerRequest* req){
  config.setOtaUpdateFlag(true);
  String resp = "Flag OTA activé - redémarrez pour tester l'email";
  req->send(200, "text/plain", resp);
});
```

## Mécanisme de fonctionnement corrigé

### Mise à jour OTA via serveur distant

1. `checkForOtaUpdate()` télécharge et applique la mise à jour
2. `esp_ota_mark_app_valid_cancel_rollback()` valide l'image
3. `g_otaJustUpdated = true` marque qu'une mise à jour vient d'être appliquée
4. Au prochain boot, `validatePendingOta()` détecte l'image validée
5. **Nouveau** : `fetchRemoteState()` récupère les variables distantes
6. Dans `setup()`, si `g_otaJustUpdated && autoCtrl.isEmailEnabled()`, l'email est envoyé

### Mise à jour OTA via interface web ElegantOTA

1. L'utilisateur télécharge un firmware via l'interface web `/update`
2. `ElegantOTA.onEnd()` est appelé avec `success = true`
3. `config.setOtaUpdateFlag(true)` sauvegarde le flag dans la flash
4. L'ESP32 redémarre automatiquement
5. Au boot, `config.loadBouffeFlags()` charge le flag depuis la flash
6. **Nouveau** : `fetchRemoteState()` récupère les variables distantes
7. Dans `setup()`, si `config.getOtaUpdateFlag()`, l'email est envoyé (avec fallback si nécessaire)
8. `config.setOtaUpdateFlag(false)` réinitialise le flag

## Tests de validation

### Script de diagnostic
Utiliser le script `test_ota_debug.py` pour un diagnostic complet :

```bash
python test_ota_debug.py
```

### Test manuel
1. Accéder à `http://[ESP32_IP]/testota` pour activer le flag OTA
2. Redémarrer l'ESP32
3. Vérifier les logs pour confirmer l'envoi d'email

## Logs attendus

### Au démarrage normal
```
[Config] Flag OTA update: false
[App] Vérification des flags OTA...
[App] g_otaJustUpdated: false
[App] config.getOtaUpdateFlag(): false
[App] autoCtrl.isEmailEnabled(): true
```

### Après une mise à jour OTA avec email activé
```
[Config] Flag OTA update: true
[App] Vérification des flags OTA...
[App] g_otaJustUpdated: false
[App] config.getOtaUpdateFlag(): true
[App] autoCtrl.isEmailEnabled(): true
[App] Envoi email pour mise à jour OTA interface web...
[App] Email interface web envoyé
```

### Après une mise à jour OTA avec email désactivé
```
[Config] Flag OTA update: true
[App] Vérification des flags OTA...
[App] g_otaJustUpdated: false
[App] config.getOtaUpdateFlag(): true
[App] autoCtrl.isEmailEnabled(): false
[App] Email non activé dans les variables distantes, utilisation de l'adresse par défaut...
[App] Email interface web envoyé
```

## Version

Cette correction est appliquée dans la version **6.2** du firmware. 