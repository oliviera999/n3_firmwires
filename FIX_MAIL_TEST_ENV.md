# 🔧 FIX : Activation des emails dans l'environnement de test

**Date**: 2025-10-14  
**Priorité**: 🟡 IMPORTANTE  
**Impact**: Environnements `s3-test` et `s3-prod`

---

## 🐛 PROBLÈME IDENTIFIÉ

### Symptômes
- ❌ **Emails désactivés** dans l'environnement de test par défaut (`s3-test`)
- ❌ **Aucun mail de veille/réveil** envoyé
- ❌ **Aucun mail de test** au démarrage
- ❌ Même problème dans `s3-prod`

### Logs observés
```
[Mail] Désactivé (FEATURE_MAIL=0)
[App] Échec de l'envoi du mail de test ✗
[App] autoCtrl.isEmailEnabled(): true  ← Configuration serveur OK
```

**Paradoxe** : 
- La configuration serveur indique `emailEnabled: true`
- Mais le système de mail est désactivé à la compilation (`FEATURE_MAIL=0`)

---

## 🔍 CAUSE RACINE

### Analyse de `platformio.ini`

| Environnement | `-DFEATURE_MAIL=1` ? | Lib ESP Mail Client ? | Emails OK ? |
|---------------|---------------------|----------------------|-------------|
| `wroom-test` | ✅ Ligne 116 | ✅ Oui | ✅ OUI |
| `wroom-prod` | ✅ Ligne 67 | ✅ Oui | ✅ OUI |
| **`s3-test`** | ❌ **MANQUANT** | ✅ Oui | ❌ **NON** |
| **`s3-prod`** | ❌ **MANQUANT** | ❌ **MANQUANT** | ❌ **NON** |

**Problème** : Les environnements ESP32-S3 n'avaient pas le flag de compilation `-DFEATURE_MAIL=1` défini.

### Impact sur le code

Quand `FEATURE_MAIL` n'est pas défini, le préprocesseur désactive :
- `mailer.begin()` → retourne immédiatement
- `mailer.send()` → retourne `false`
- `mailer.sendAlert()` → ne fait rien
- `mailer.sendSleepMail()` → stub vide
- `mailer.sendWakeMail()` → stub vide

Voir `src/mailer.cpp:428-439` :
```cpp
#else  // FEATURE_MAIL pas défini
bool Mailer::begin() { Serial.println("[Mail] Désactivé (FEATURE_MAIL=0)"); return true; }
bool Mailer::send(const char*, const char*, const char*, const char*) { return false; }
// ... tous les stubs ...
#endif
```

---

## ✅ SOLUTION IMPLÉMENTÉE

### Modifications apportées

**Fichier** : `platformio.ini`

#### 1. Environnement `s3-test` (ligne 202-221)

**AVANT** :
```ini
[env:s3-test]
board = esp32-s3-devkitc-1
board_build.partitions = partitions_esp32s3_8MB_ota_xlarge.csv
build_flags = 
	${env.build_flags}
	-DBOARD_S3
	-DPROFILE_TEST
	-DOLED_DIAGNOSTIC
lib_deps = 
	me-no-dev/ESPAsyncWebServer@3.6.0
	...
```

**APRÈS** :
```ini
[env:s3-test]
board = esp32-s3-devkitc-1
board_build.partitions = partitions_esp32s3_8MB_ota_xlarge.csv
build_flags = 
	${env.build_flags}
	-DBOARD_S3
	-DPROFILE_TEST
	-DOLED_DIAGNOSTIC
	-DFEATURE_MAIL=1          ← AJOUTÉ
lib_deps = 
	me-no-dev/ESPAsyncWebServer@3.6.0
	...
```

#### 2. Environnement `s3-prod` (ligne 182-201)

**AVANT** :
```ini
[env:s3-prod]
board = esp32-s3-devkitc-1
board_build.partitions = partitions_esp32s3_8MB_ota_xlarge.csv
build_flags = 
	${env.build_flags}
	-DBOARD_S3
	-DPROFILE_PROD
	-DNDEBUG
lib_deps = 
	me-no-dev/ESPAsyncWebServer@3.6.0
	bblanchon/ArduinoJson@^7.4.2
	madhephaestus/ESP32Servo@^3.0.9
	adafruit/DHT sensor library@^1.4.6
	adafruit/Adafruit SSD1306@^2.5.15
	milesburton/DallasTemperature@^3.11.0
	...
```

**APRÈS** :
```ini
[env:s3-prod]
board = esp32-s3-devkitc-1
board_build.partitions = partitions_esp32s3_8MB_ota_xlarge.csv
build_flags = 
	${env.build_flags}
	-DBOARD_S3
	-DPROFILE_PROD
	-DNDEBUG
	-DFEATURE_MAIL=1                        ← AJOUTÉ
lib_deps = 
	me-no-dev/ESPAsyncWebServer@3.6.0
	bblanchon/ArduinoJson@^7.4.2
	madhephaestus/ESP32Servo@^3.0.9
	adafruit/DHT sensor library@^1.4.6
	adafruit/Adafruit SSD1306@^2.5.15
	mobizt/ESP Mail Client@^3.4.24          ← AJOUTÉ
	milesburton/DallasTemperature@^3.11.0
	...
```

---

## 📊 IMPACT ET BÉNÉFICES

### Avant le fix

| Fonctionnalité | `s3-test` | `s3-prod` |
|----------------|-----------|-----------|
| Mail de test au boot | ❌ Désactivé | ❌ Désactivé |
| Mail de mise en veille | ❌ Désactivé | ❌ Désactivé |
| Mail de réveil | ❌ Désactivé | ❌ Désactivé |
| Mail de nourrissage | ❌ Désactivé | ❌ Désactivé |
| Mail d'alerte (flood, etc.) | ❌ Désactivé | ❌ Désactivé |
| Mail OTA | ❌ Désactivé | ❌ Désactivé |
| Digest périodique | ❌ Désactivé | ❌ Désactivé |

### Après le fix

| Fonctionnalité | `s3-test` | `s3-prod` |
|----------------|-----------|-----------|
| Mail de test au boot | ✅ **Activé** | ✅ **Activé** |
| Mail de mise en veille | ✅ **Activé** | ✅ **Activé** |
| Mail de réveil | ✅ **Activé** | ✅ **Activé** |
| Mail de nourrissage | ✅ **Activé** | ✅ **Activé** |
| Mail d'alerte (flood, etc.) | ✅ **Activé** | ✅ **Activé** |
| Mail OTA | ✅ **Activé** | ✅ **Activé** |
| Digest périodique | ✅ **Activé** | ✅ **Activé** |

---

## 🧪 TESTS À EFFECTUER

### 1. Test de compilation (obligatoire)

```bash
# Test environnement s3-test
pio run --environment s3-test

# Test environnement s3-prod
pio run --environment s3-prod
```

**Attendu** :
- ✅ Compilation réussie sans erreur
- ✅ Bibliothèque `ESP Mail Client` incluse
- ✅ Macro `FEATURE_MAIL` définie à 1

### 2. Test du mail de démarrage

```bash
# Flash et monitorer
pio run --environment s3-test --target upload && pio device monitor
```

**Logs attendus** (au lieu de `FEATURE_MAIL=0`) :

```
[Power] Synchronisation NTP en cours...
[Power] Heure synchronisée NTP: 01:00:09 01/01/2025
[Mail] Initialisation SMTP... ✔                    ← NOUVEAU
[Mail] Configuration: smtp.gmail.com:465 (SSL)     ← NOUVEAU
[App] Envoi du mail de test au démarrage...        
[Mail] ===== DÉTAILS DU MAIL =====                 ← NOUVEAU
[Mail] De: FFP3CS Boot <arnould.svt@gmail.com>     ← NOUVEAU
[Mail] À: User <[adresse configurée]>              ← NOUVEAU
[Mail] Objet: FFP3CS - Boot Test                   ← NOUVEAU
[Mail] Message envoyé avec succès ✔                ← AU LIEU DE "Échec"
```

### 3. Test de mise en veille (après ~150 secondes)

**Logs attendus** :

```
[Auto] Entrée en light-sleep pour ~1200 s
[Auto] 📧 Envoi du mail de mise en veille (heap: XXXXX bytes)...
[Mail] ===== ENVOI MAIL VEILLE =====
[Mail] Raison: Délai écoulé (éveillé 150s, cible 150s)
[Mail] Durée: 1200 s
[Mail] ⚡ Utilisation des dernières lectures (pas de nouvelle lecture capteurs)
[Mail] Message envoyé avec succès ✔                ← IMPORTANT
[Auto] Mail de mise en veille envoyé avec succès ✔
```

### 4. Test de réveil (après ~1200 secondes de sleep)

**Logs attendus** :

```
[Auto] 📧 Envoi du mail de réveil (heap: XXXXX bytes)...
[Mail] ===== ENVOI MAIL RÉVEIL =====
[Mail] Raison: Réveil automatique par timer
[Mail] Durée veille: 1200 s
[Mail] Message envoyé avec succès ✔
[Auto] Mail de réveil envoyé avec succès ✔
```

### 5. Vérification configuration email activée

**Vérifier dans les logs** :

```
[App] autoCtrl.isEmailEnabled(): true
```

**Vérifier via API** (si interface web accessible) :
```bash
curl http://[IP_ESP32]/api/status | jq '.automatism.emailEnabled'
# Attendu: true
```

---

## 📝 CHECKLIST POST-DÉPLOIEMENT

### Compilation
- [ ] `pio run --environment s3-test` → SUCCESS
- [ ] `pio run --environment s3-prod` → SUCCESS
- [ ] Pas d'erreur de lien (undefined reference)
- [ ] Bibliothèque `ESP Mail Client` incluse

### Tests runtime
- [ ] **Flash** du firmware sur ESP32-S3
- [ ] **Monitoring 90 secondes** après flash
- [ ] **Vérification log** : `[Mail] Initialisation SMTP... ✔` (au lieu de `FEATURE_MAIL=0`)
- [ ] **Mail de test** : envoyé avec succès au boot
- [ ] **Mail de veille** : envoyé après ~150s d'éveil
- [ ] **Mail de réveil** : envoyé après réveil du sleep
- [ ] **Pas de crash** lié aux mails (vérifier compteur reboots stable)

### Validation fonctionnelle
- [ ] Email reçu dans la boîte mail configurée
- [ ] Contenu des mails cohérent (valeurs capteurs, etc.)
- [ ] Pas de timeout SMTP (délai < 10 secondes)
- [ ] Heap suffisant avant envoi (> 45KB)

---

## ⚠️ POINTS D'ATTENTION

### 1. Configuration SMTP requise

Les emails nécessitent une configuration SMTP valide dans `include/secrets.h` :

```cpp
constexpr const char* AUTHOR_EMAIL = "arnould.svt@gmail.com";
constexpr const char* AUTHOR_PASSWORD = "ddbfvlkssfleypdr";  // App Password Gmail
```

**Vérifier** :
- ✅ Compte Gmail avec **"Accès moins sécurisé"** OU **"Mot de passe d'application"** activé
- ✅ Pare-feu/routeur autorise SMTP sortant (port 465)
- ✅ WiFi connecté avant tentative d'envoi

### 2. Mémoire (Heap)

Les mails ne sont envoyés QUE si :
- `WiFi.status() == WL_CONNECTED` → WiFi connecté
- `autoCtrl.isEmailEnabled()` → Email activé dans la config serveur
- `ESP.getFreeHeap() >= 45000` → Au moins 45KB de heap disponible

**Si heap insuffisant**, log attendu :
```
[Auto] ⚠️ Skip mail de mise en veille: heap insuffisant (42000 < 45000 bytes)
```

### 3. Délai d'envoi

L'envoi d'un email SMTP peut prendre **2-8 secondes**. Pendant ce temps :
- Watchdog doit être réinitialisé (déjà géré dans le code)
- Pas d'autres opérations bloquantes en parallèle
- Connexion WiFi doit rester stable

### 4. Mise en veille optimisée

Le fix `CRASH_SLEEP_FIX_V11.06` a été implémenté :
- ✅ Les mails de sleep/wake **n'appellent PLUS `sensors.read()`**
- ✅ Utilisation de `_lastReadings` (cache) → pas de blocage DHT22
- ✅ Temps d'envoi réduit : ~15s → ~2-3s

---

## 🚨 ROLLBACK (si nécessaire)

Si problème après déploiement :

```bash
# Revenir à la version précédente de platformio.ini
git checkout HEAD~1 -- platformio.ini

# Recompiler
pio run --environment s3-test --target upload
```

**Alternative** : Désactiver temporairement les mails sans rollback :

Dans `platformio.ini`, remplacer :
```ini
-DFEATURE_MAIL=1
```
par :
```ini
-DFEATURE_MAIL=0
```

---

## 📚 FICHIERS MODIFIÉS

```
platformio.ini                   (environments s3-test et s3-prod)
FIX_MAIL_TEST_ENV.md            (ce document)
```

---

## 💡 CONTEXTE ADDITIONNEL

### Historique des problèmes de mails

1. **v11.05** : Crash lors de l'envoi des mails de veille
   - **Cause** : Lecture bloquante des capteurs (14s DHT22)
   - **Fix** : v11.06 - Utilisation de `_lastReadings` au lieu de `sensors.read()`
   - **Référence** : `CRASH_SLEEP_FIX_V11.06.md`

2. **v11.06** : Mails désactivés dans `s3-test` et `s3-prod`
   - **Cause** : Flag `-DFEATURE_MAIL=1` manquant dans `platformio.ini`
   - **Fix** : Ce document (2025-10-14)

### Conditions d'envoi des mails

**Code source** (`src/automatism.cpp:2337-2364`) :

```cpp
#if FEATURE_MAIL  // ← Doit être = 1 à la compilation
if (WiFi.status() == WL_CONNECTED && autoCtrl.isEmailEnabled()) {
  if (heapBeforeMail >= MIN_HEAP_FOR_MAIL) {
    // Envoi du mail
  } else {
    Serial.printf("⚠️ Skip mail: heap insuffisant\n");
  }
}
#endif
```

**Trois conditions cumulatives** :
1. `FEATURE_MAIL=1` → Défini à la compilation (platformio.ini)
2. `WiFi.status() == WL_CONNECTED` → Runtime
3. `autoCtrl.isEmailEnabled()` → Configuration serveur distant
4. `ESP.getFreeHeap() >= 45000` → Mémoire disponible

---

## 🎯 RÉSULTAT ATTENDU

**Après ce fix, l'ESP32-S3 devrait** :
- ✅ Envoyer un mail de test au boot
- ✅ Envoyer un mail avant chaque mise en veille (~150s)
- ✅ Envoyer un mail après chaque réveil (~1200s)
- ✅ Envoyer des mails d'alerte (flood, nourrissage, etc.)
- ✅ Envoyer un digest périodique (toutes les 24h)
- ✅ **Pas de crash lié aux mails** (fix v11.06 déjà en place)

---

**Status** : ✅ PRÊT À TESTER  
**Impact** : Environnements ESP32-S3 uniquement (`s3-test`, `s3-prod`)  
**Rétro-compatibilité** : Aucun impact sur `wroom-test` et `wroom-prod` (déjà fonctionnels)  
**Prochain step** : Recompiler et tester sur hardware ESP32-S3





