# Rapport d'Analyse Exhaustive - Code Mort FFP5CS

**Date**: 2026-01-17  
**Version du projet**: v11.146+  
**Objectif**: Identifier tout le code mort sans modifier le code

---

## Résumé Exécutif

Cette analyse a identifié **plusieurs catégories de code mort** dans le projet ESP32 FFP5CS :

- **1 fichier complet exclu du build** : `timer_manager.cpp` (utilisé uniquement en tests)
- **1 fichier avec stubs complets** : `automatism_refill.cpp` (ancienne implémentation remplacée)
- **1 méthode jamais appelée** : `SystemActuators::startServoSequence()`
- **Code conditionnel inactif** : Stubs `FEATURE_MAIL=0` (jamais compilés car `FEATURE_MAIL=1`)
- **Code conditionnel BOARD_S3** : Jamais compilé (seul BOARD_WROOM utilisé)

**Estimation totale** : ~500-600 lignes de code mort identifiées

---

## 1. Fichiers Exclus du Build

### 1.1 `src/timer_manager.cpp`

**Statut** : ❌ **CODE MORT** (exclu du build principal)

**Preuve** :
- Exclu via `build_src_filter = -<timer_manager.cpp>` dans `platformio.ini` ligne 25
- Utilisé uniquement dans l'environnement `native` (tests unitaires) ligne 157
- Le header `include/timer_manager.h` n'est référencé nulle part dans le code source principal

**Détails** :
- Fichier de 216 lignes
- Classe `TimerManager` complète avec toutes ses méthodes
- Utilisé uniquement dans `test/test_timer_manager/test_main.cpp`

**Recommandation** :
- ✅ **CONSERVER** pour les tests unitaires (environnement `native`)
- ⚠️ Le header `timer_manager.h` pourrait être déplacé dans `test/unit/` si jamais utilisé ailleurs

**Impact** : Aucun (fichier déjà exclu du build)

---

## 2. Méthodes Vides et Stubs

### 2.1 `src/automatism/automatism_refill.cpp`

**Statut** : ❌ **CODE MORT COMPLET** (ancienne implémentation)

**Preuve** :
- Toutes les méthodes sont des stubs vides avec `(void)` casts
- Le header référencé `automatism/automatism_refill.h` existe dans `src/automatism/` (pas dans `include/`)
- Une nouvelle implémentation existe : `automatism_refill_controller.cpp` utilisée dans `automatism.h`
- Aucune référence à ce fichier dans `platformio.ini` ou ailleurs

**Méthodes stub identifiées** (90 lignes) :
- `tick()` - ligne 27
- `startManual()` - ligne 33
- `stopManual()` - ligne 38
- `evaluateFloodAlerts()` - ligne 42
- `evaluateLocks()` - ligne 48
- `processStart()` - ligne 54
- `processRunning()` - ligne 60
- `processStop()` - ligne 66
- `processRetries()` - ligne 73
- `notifyServer()` - ligne 79
- `sendMail()` - ligne 84

**Recommandation** :
- ✅ **SUPPRIMER** `src/automatism/automatism_refill.cpp` et `src/automatism/automatism_refill.h`
- L'implémentation réelle est dans `automatism_refill_controller.cpp`

**Impact** : ~90 lignes de code mort

---

### 2.2 `src/mailer.cpp` - Stubs conditionnels `FEATURE_MAIL=0`

**Statut** : ⚠️ **CODE CONDITIONNEL JAMAIS COMPILÉ**

**Preuve** :
- Stubs définis lignes 1097-1115 dans un bloc `#else` (si `FEATURE_MAIL=0`)
- Mais `FEATURE_MAIL=1` dans `platformio.ini` ligne 47
- Ces stubs ne seront **jamais compilés** avec la configuration actuelle

**Stubs identifiés** :
- `Mailer::begin()` - ligne 1097
- `Mailer::sendSync()` - ligne 1098
- `Mailer::send()` - ligne 1099
- `Mailer::sendAlert()` - ligne 1100
- `Mailer::sendAlertSync()` - ligne 1103
- `Mailer::sendSleepMail()` - ligne 1106
- `Mailer::sendWakeMail()` - ligne 1109
- `Mailer::initMailQueue()` - ligne 1112
- `Mailer::processOneMailSync()` - ligne 1113
- `Mailer::hasPendingMails()` - ligne 1114
- `Mailer::getQueuedMails()` - ligne 1115

**Recommandation** :
- ⚠️ **CONSERVER** (code conditionnel légitime pour configuration future sans mail)
- Mais noter que ce code ne sera jamais compilé avec la config actuelle

**Impact** : ~20 lignes de code conditionnel inactif

---

### 2.3 `src/system_actuators.cpp` - `startServoSequence()`

**Statut** : ❌ **MÉTHODE JAMAIS APPELÉE**

**Preuve** :
- Méthode définie ligne 106 avec commentaire indiquant qu'elle ne devrait pas exister
- Commentaire ligne 99 : "Mais dans automatism_feeding.cpp on appelle _acts.startServoSequence(duration)"
- **Aucun appel trouvé** dans le codebase avec `grep "startServoSequence"`
- Seulement la définition et le commentaire

**Code** :
```cpp
void SystemActuators::startServoSequence(uint16_t durationSec) {
    LOG(LOG_WARN, "startServoSequence called without feeder ID - Defaulting to Small");
    feedSmallFish(durationSec);
}
```

**Recommandation** :
- ✅ **SUPPRIMER** la méthode `startServoSequence()` de `system_actuators.cpp` et `system_actuators.h`
- Le commentaire indique que le code appelant devrait utiliser `feedSmallFish()` ou `feedBigFish()` directement

**Impact** : ~10 lignes de code mort

---

## 3. Fonctions Jamais Appelées

### 3.1 `SystemActuators::startServoSequence()`

Voir section 2.3 ci-dessus.

### 3.2 Méthodes de `AutomatismRefillController` (ancienne version)

Toutes les méthodes dans `automatism_refill.cpp` sont des stubs jamais appelés (voir section 2.1).

---

## 4. Code Conditionnel Inactif

### 4.1 `FEATURE_MAIL=0` - Stubs Mailer

Voir section 2.2 ci-dessus.

### 4.2 `FEATURE_OLED=0` - Code DisplayView

**Statut** : ⚠️ **CODE CONDITIONNEL JAMAIS COMPILÉ**

**Preuve** :
- Code conditionnel dans `display_view.cpp` ligne 222 : `#if FEATURE_OLED == 0`
- Mais `FEATURE_OLED=1` dans `platformio.ini` ligne 46
- Ce code ne sera jamais compilé

**Recommandation** :
- ⚠️ **CONSERVER** (code conditionnel légitime pour configuration sans OLED)

**Impact** : ~5 lignes de code conditionnel inactif

---

### 4.3 `FEATURE_WIFI_APSTA=0` - Code WiFi AP+STA

**Statut** : ⚠️ **CODE CONDITIONNEL JAMAIS COMPILÉ**

**Preuve** :
- `FEATURE_WIFI_APSTA=0` dans `platformio.ini` ligne 50
- Code conditionnel dans `wifi_manager.cpp` lignes 26, 173, 219 : `if (FEATURE_WIFI_APSTA)`
- Ces blocs ne seront jamais exécutés

**Recommandation** :
- ⚠️ **CONSERVER** (code conditionnel légitime pour activation future du mode AP+STA)

**Impact** : ~30 lignes de code conditionnel inactif

---

### 4.4 `BOARD_S3` - Code spécifique ESP32-S3

**Statut** : ⚠️ **CODE CONDITIONNEL JAMAIS COMPILÉ**

**Preuve** :
- Code conditionnel `#if defined(BOARD_S3)` dans :
  - `mailer.cpp` lignes 146, 219
  - `ota_manager.cpp` lignes 166, 243
- Seul `BOARD_WROOM` est défini dans `platformio.ini` (ligne 83, 115)
- Ce code ne sera jamais compilé avec la configuration actuelle

**Recommandation** :
- ⚠️ **CONSERVER** (code conditionnel légitime pour support futur ESP32-S3)

**Impact** : ~20 lignes de code conditionnel inactif

---

### 4.5 `DISABLE_ASYNC_WEBSERVER` - Stubs WebServer

**Statut** : ⚠️ **CODE CONDITIONNEL JAMAIS COMPILÉ**

**Preuve** :
- Stubs définis dans :
  - `web_server_context.cpp` ligne 145
  - `web_routes_status.cpp` ligne 511
  - `web_routes_ui.cpp` ligne 357
- `DISABLE_ASYNC_WEBSERVER` n'est défini que dans l'environnement `native` (tests) ligne 170
- Jamais défini dans les environnements de production/test ESP32

**Recommandation** :
- ⚠️ **CONSERVER** (code conditionnel légitime pour tests unitaires)

**Impact** : ~10 lignes de code conditionnel inactif

---

## 5. Commentaires TODO/FIXME - Analyse Détaillée

### 5.1 `src/automatism/automatism_sleep.cpp` ligne 387

**Localisation** : `src/automatism/automatism_sleep.cpp:387`

**Code complet** :
```cpp
bool AutomatismSleep::process(const SensorReadings& r, SystemActuators& acts, Automatism& core) {
    // Cette méthode remplace handleAutoSleep() en centralisant la logique
    // Pour l'instant, on redirige vers handleAutoSleep() pour compatibilité
    handleAutoSleep(r, acts, core);
    return false; // TODO: Retourner true si sleep exécuté
}
```

**Contexte** :
- Méthode publique `process()` déclarée dans `include/automatism/automatism_sleep.h` ligne 21
- Signature : `bool process(const SensorReadings& r, SystemActuators& acts, Automatism& core);`
- Documentation du header indique : "Renvoie true si le système est entré en veille"
- Actuellement, la méthode délègue à `handleAutoSleep()` qui est `void` (ne retourne rien)
- La méthode retourne toujours `false`, même si le système entre en veille

**Analyse du flux d'exécution** :
1. `process()` est appelée depuis `Automatism::update()` ligne 99
2. `process()` appelle `handleAutoSleep()` qui contient toute la logique de veille
3. `handleAutoSleep()` peut effectivement mettre le système en veille (ligne 470+)
4. Mais `process()` retourne toujours `false`, ignorant le résultat

**Impact** :
- ⚠️ **Fonctionnel mais incomplet** : Le système entre bien en veille, mais l'appelant ne peut pas savoir si la veille a été exécutée
- ⚠️ **Incohérence API** : La signature promet un retour booléen mais ne l'utilise pas
- ⚠️ **Pas d'impact critique** : Le code appelant (`automatism.cpp:99`) n'utilise pas la valeur de retour

**Code appelant** :
```cpp
// src/automatism.cpp:99
_sleep.process(r, _acts, *this);  // Valeur de retour ignorée
```

**Recommandation** :
- ✅ **IMPLÉMENTER** : Modifier `handleAutoSleep()` pour retourner un booléen, ou ajouter une variable de suivi dans `process()`
- ✅ **PRIORITÉ MOYENNE** : Améliorer la cohérence de l'API, mais pas critique car valeur non utilisée
- ⚠️ **ALTERNATIVE** : Si la valeur de retour n'est jamais utilisée, changer la signature en `void` et supprimer le TODO

**Estimation effort** : 15-30 minutes (modification de `handleAutoSleep()` pour retourner bool)

---

### 5.2 `src/automatism/automatism_sync.cpp` ligne 124

**Localisation** : `src/automatism/automatism_sync.cpp:124`

**Code complet** :
```cpp
if (triggerSmall) {
    Serial.println(F("[Sync] 🐟 Commande nourrissage PETITS reçue du serveur distant"));
    autoCtrl.manualFeedSmall();
    sendCommandAck("bouffePetits", "executed");
    logRemoteCommandExecution("fd_small", true);
    tryResetRemoteFlags("bouffePetits=0&108=0");
    
    if (_emailEnabled) {
        char messageBuffer[256];
        autoCtrl.createFeedingMessage(messageBuffer, sizeof(messageBuffer),
            "Bouffe manuelle - Petits poissons",
            autoCtrl.getFeedBigDur(), autoCtrl.getFeedSmallDur());
        // TODO: Envoyer l'email via autoCtrl (qui délègue au Mailer)
    }
    autoCtrl.armMailBlink();
}
```

**Contexte** :
- Code dans `handleRemoteFeedingCommands()` qui traite les commandes de nourrissage distantes
- Un message est créé avec `createFeedingMessage()` mais jamais envoyé
- Le même pattern existe pour `triggerBig` (ligne 145-150) avec le même TODO implicite
- Les emails de nourrissage manuelle sont envoyés ailleurs (via `web_server.cpp` ligne 223)

**Analyse du flux** :
1. Commande reçue du serveur distant (`bouffePetits` ou `bouffeGros`)
2. Nourrissage déclenché via `manualFeedSmall()` ou `manualFeedBig()`
3. Message préparé dans `messageBuffer` mais **jamais envoyé**
4. Seul `armMailBlink()` est appelé (clignotement OLED)

**Comparaison avec autres endroits** :
- ✅ **`web_server.cpp:223`** : Envoie email via `sendManualActionEmail()` pour nourrissage web
- ✅ **`automatism_feeding_schedule.cpp:149`** : Envoie email via `_mailer.send()` pour nourrissage automatique
- ❌ **`automatism_sync.cpp:124`** : Message créé mais **jamais envoyé**

**Impact** :
- ⚠️ **Fonctionnalité manquante** : Les nourrissages déclenchés depuis le serveur distant ne génèrent pas d'email
- ⚠️ **Incohérence** : Les nourrissages manuels via web génèrent des emails, mais pas ceux via serveur distant
- ⚠️ **Pas critique** : Le système fonctionne, mais l'utilisateur ne reçoit pas de notification email

**Solution proposée** :
```cpp
if (_emailEnabled) {
    char messageBuffer[256];
    autoCtrl.createFeedingMessage(messageBuffer, sizeof(messageBuffer),
        "Bouffe manuelle - Petits poissons",
        autoCtrl.getFeedBigDur(), autoCtrl.getFeedSmallDur());
    // Envoyer l'email via autoCtrl (qui délègue au Mailer)
    autoCtrl._mailer.send("Nourrissage manuel - Petits poissons", 
                          messageBuffer, 
                          "System", 
                          autoCtrl.getEmailAddress());
}
```

**Recommandation** :
- ✅ **IMPLÉMENTER** : Ajouter l'envoi d'email après création du message
- ✅ **PRIORITÉ MOYENNE** : Améliorer la cohérence fonctionnelle, pas critique
- ⚠️ **VÉRIFIER** : S'assurer que `autoCtrl` expose un accès au `Mailer` (probablement via `_mailer` ou méthode publique)

**Estimation effort** : 30-60 minutes (ajouter l'envoi d'email + tester)

**Note** : Le même TODO existe implicitement pour `triggerBig` (ligne 145-150), à corriger également.

---

### 5.3 Résumé des TODOs

| TODO | Fichier | Ligne | Priorité | Impact | Effort Estimé |
|------|---------|-------|----------|--------|---------------|
| Retour booléen sleep | `automatism_sleep.cpp` | 387 | Moyenne | API incohérente | 15-30 min |
| Email nourrissage distant | `automatism_sync.cpp` | 124 | Moyenne | Fonctionnalité manquante | 30-60 min |

**Total TODOs identifiés** : 2 (plus 1 implicite pour `triggerBig`)

**Recommandation globale** :
- Les deux TODOs sont **non-critiques** mais représentent des améliorations de cohérence
- Aucun TODO ne bloque le fonctionnement du système
- Priorité moyenne pour améliorer la qualité du code et la cohérence fonctionnelle

---

## 6. Includes Non Utilisés

**Méthode d'analyse** : Vérification manuelle des includes dans les fichiers principaux

**Résultat** : Tous les includes semblent être utilisés. Aucun include non utilisé identifié de manière certaine.

**Note** : Une analyse plus approfondie avec un outil statique (ex: `include-what-you-use`) pourrait révéler des includes non utilisés, mais cela dépasse le scope de cette analyse manuelle.

---

## 7. Dépendances Non Utilisées

### Analyse des bibliothèques dans `platformio.ini`

Toutes les dépendances listées sont **utilisées** :

1. ✅ **ESPAsyncWebServer@3.5.0** - Utilisé dans `web_server.cpp`, `web_routes_*.cpp`
2. ✅ **Adafruit Unified Sensor@1.1.14** - Dépendance transitive de DHT sensor library
3. ✅ **DHT sensor library@1.4.6** - Utilisé dans `sensors.cpp` pour DHT22/DHT11
4. ✅ **Adafruit GFX Library@1.11.10** - Dépendance transitive de SSD1306
5. ✅ **Adafruit SSD1306@2.5.9** - Utilisé dans `display_view.cpp`, `display_renderers.cpp`
6. ✅ **Adafruit BusIO@1.17.4** - Dépendance transitive de SSD1306
7. ✅ **OneWire@2.3.8** - Utilisé dans `sensors.cpp` pour DS18B20
8. ✅ **DallasTemperature@3.11.0** - Utilisé dans `sensors.cpp` pour DS18B20
9. ✅ **ESP32Servo@3.0.5** - Utilisé dans `actuators.cpp` pour les servos de nourrissage
10. ✅ **ESP32Time@2.0.0** - Utilisé dans `power.cpp` pour la gestion du temps
11. ✅ **ArduinoJson@7.4.2** - Utilisé massivement dans tout le projet (web_server, web_client, automatism, etc.)
12. ✅ **ESP Mail Client@3.4.24** - Utilisé dans `mailer.cpp` pour l'envoi SMTP
13. ✅ **WebSockets@2.7.0** - Utilisé dans `realtime_websocket.cpp` pour WebSocket temps réel

**Conclusion** : Aucune dépendance inutilisée identifiée.

---

## 8. Variables et Membres Non Utilisés

### 8.1 Variables dans `src/app.cpp`

**Variables analysées** (lignes 65-71) :
- `g_hostname` - ✅ Utilisé
- `g_otaJustUpdated` - ✅ Utilisé
- `g_previousVersion` - ✅ Utilisé
- `g_lastOtaCheck` - ✅ Utilisé
- `g_lastDigestMs` - ✅ Utilisé
- `g_lastDigestSeq` - ✅ Utilisé

**Conclusion** : Toutes les variables globales sont utilisées.

---

## 9. Headers Sans Implémentation

### 9.1 `include/timer_manager.h`

**Statut** : ⚠️ **Header avec implémentation exclue du build**

**Preuve** :
- Header existe dans `include/timer_manager.h`
- Implémentation dans `src/timer_manager.cpp` exclue du build principal
- Utilisé uniquement dans les tests unitaires

**Recommandation** :
- ✅ **CONSERVER** (utilisé en tests)

---

## 10. Statistiques Globales

### Code Mort Certain (peut être supprimé)

| Fichier/Méthode | Lignes | Priorité |
|----------------|--------|----------|
| `src/automatism/automatism_refill.cpp` | ~90 | Haute |
| `src/automatism/automatism_refill.h` | ~95 | Haute |
| `SystemActuators::startServoSequence()` | ~10 | Moyenne |
| **TOTAL** | **~195** | |

### Code Conditionnel Inactif (garder pour futures configs)

| Type | Lignes | Raison |
|------|-------|--------|
| Stubs `FEATURE_MAIL=0` | ~20 | Config future sans mail |
| Code `FEATURE_OLED=0` | ~5 | Config future sans OLED |
| Code `FEATURE_WIFI_APSTA=0` | ~30 | Config future AP+STA |
| Code `BOARD_S3` | ~20 | Support futur ESP32-S3 |
| Stubs `DISABLE_ASYNC_WEBSERVER` | ~10 | Tests unitaires |
| **TOTAL** | **~85** | |

### Code Utilisé Uniquement en Tests

| Fichier | Lignes | Statut |
|---------|--------|--------|
| `src/timer_manager.cpp` | ~216 | Tests unitaires uniquement |

---

## 11. Recommandations Finales

### Priorité Haute (Suppression Recommandée)

1. ✅ **SUPPRIMER** `src/automatism/automatism_refill.cpp` et `src/automatism/automatism_refill.h`
   - Ancienne implémentation remplacée par `automatism_refill_controller.cpp`
   - Toutes les méthodes sont des stubs vides
   - **Impact** : ~185 lignes de code mort

2. ✅ **SUPPRIMER** `SystemActuators::startServoSequence()` de `system_actuators.cpp` et `system_actuators.h`
   - Méthode jamais appelée
   - Commentaire indique qu'elle ne devrait pas exister
   - **Impact** : ~10 lignes de code mort

### Priorité Moyenne (Vérification)

3. ⚠️ **VÉRIFIER** les TODOs dans :
   - `automatism_sleep.cpp` ligne 387
   - `automatism_sync.cpp` ligne 124

### Priorité Basse (Conserver)

4. ✅ **CONSERVER** le code conditionnel inactif :
   - Stubs `FEATURE_MAIL=0` (config future)
   - Code `FEATURE_OLED=0` (config future)
   - Code `FEATURE_WIFI_APSTA=0` (config future)
   - Code `BOARD_S3` (support futur)
   - Stubs `DISABLE_ASYNC_WEBSERVER` (tests)

5. ✅ **CONSERVER** `timer_manager.cpp` (utilisé en tests unitaires)

---

## 12. Impact Estimé

### Espace Mémoire Libéré (si suppressions effectuées)

- **Code mort certain** : ~195 lignes
- **Estimation Flash** : ~2-3 KB (compilé)
- **Estimation RAM** : Négligeable (code statique)

### Complexité Réduite

- **Fichiers supprimés** : 2 fichiers
- **Méthodes supprimées** : 1 méthode publique
- **Maintenabilité** : Améliorée (moins de code à maintenir)

---

## 13. Conclusion

L'analyse a identifié **~195 lignes de code mort certain** qui peuvent être supprimées sans risque :

1. L'ancienne implémentation `automatism_refill.cpp/h` (remplacée)
2. La méthode `startServoSequence()` jamais appelée

Le reste du code identifié est soit :
- Du code conditionnel légitime pour futures configurations
- Du code utilisé uniquement en tests unitaires

**Recommandation globale** : Procéder aux suppressions de priorité haute pour nettoyer le codebase tout en conservant le code conditionnel pour la flexibilité future.

---

**Fin du rapport**
