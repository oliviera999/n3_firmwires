# Rapport de Configuration - Système de Mail
## Date: 25 janvier 2026

---

## 📋 Résumé Exécutif

**Statut:** ✅ **SYSTÈME DE MAIL CONFIGURÉ ET ACTIVÉ** mais **non utilisé** pendant le monitoring

Le système de mail est correctement configuré dans le code et activé via `FEATURE_MAIL=1`, mais aucune activité mail n'a été détectée pendant les 15 minutes de monitoring. Cela indique que soit les mails sont désactivés dans la configuration runtime, soit aucun événement déclencheur n'est survenu.

---

## ✅ Configuration Compilée

### Feature Flag
- **FEATURE_MAIL:** ✅ **ACTIVÉ** (`-DFEATURE_MAIL=1` dans `platformio.ini`)
- **Statut:** Le code de mail est compilé et inclus dans le firmware

### Initialisation
- **Localisation:** `src/bootstrap_network.cpp::onWifiReady()`
- **Condition:** Initialisé uniquement si WiFi connecté
- **Séquence:**
  1. `mailer.begin()` - Configuration SMTP
  2. `mailer.initMailQueue()` - Initialisation queue asynchrone
  3. Log: `[Mail] ===== INITIALISATION MAILER =====`

### Configuration SMTP
- **Host:** `EmailConfig::SMTP_HOST`
- **Port:** `EmailConfig::SMTP_PORT`
- **Email expéditeur:** `Secrets::AUTHOR_EMAIL`
- **Mot de passe:** `Secrets::AUTHOR_PASSWORD`
- **Timeout TCP:** 60 secondes
- **Reconnexion automatique:** Activée

---

## 🔍 Configuration Runtime

### Activation/Désactivation
- **Méthode:** `Automatism::isEmailEnabled()` → retourne `mailNotif`
- **Valeur par défaut:** `true` (ligne 271 de `automatism.h`)
- **Stockage:** Variable membre `mailNotif` dans `Automatism`
- **Synchronisation:** Gérée par `AutomatismSync::_emailEnabled`

### Adresse Email Destinataire
- **Méthode:** `Automatism::getEmailAddress()` → retourne `_emailAddress`
- **Stockage:** 
  - Variable membre `_emailAddress[EmailConfig::MAX_EMAIL_LENGTH]` dans `Automatism`
  - Variable membre `_emailAddress[96]` dans `AutomatismSync`
- **Chargement:** Depuis NVS via clé `"gpio_email"` (namespace `CONFIG`)
- **Fallback:** `EmailConfig::DEFAULT_RECIPIENT` si non configuré

### Synchronisation Configuration
- **Source:** Serveur distant via `AutomatismSync::applyConfigFromJson()`
- **Clés JSON:**
  - `"mail"` → Adresse email destinataire
  - `"mailNotif"` → Activation/désactivation (true/false)
- **Stockage NVS:** Sauvegardé automatiquement après réception

---

## 📧 Événements Déclencheurs d'Envoi

### 1. Mail de Démarrage (Boot)
- **Localisation:** `src/app.cpp::setup()` (lignes 180-234)
- **Condition:** WiFi connecté au boot
- **Méthode:** `mailer.sendAlert()`
- **Sujet:** `"Démarrage système v{VERSION}"`
- **Destinataire:** Adresse configurée ou fallback `"oliv.arn.lau@gmail.com"`
- **Log attendu:** `[Mail] ===== SENDALERT ASYNC ====="` ou `"=== TEST MAIL AU DÉMARRAGE ==="`

### 2. Nourrissage Automatique
- **Localisation:** `src/automatism/automatism_feeding_schedule.cpp`
- **Déclencheurs:**
  - Matin (heure configurée, défaut: 8h)
  - Midi (heure configurée, défaut: 12h)
  - Soir (heure configurée, défaut: 19h)
- **Condition:** `mailNotif == true` ET adresse email valide
- **Méthode:** `sendFeedingEmail()`

### 3. Nourrissage Manuel
- **Localisation:** `src/automatism/automatism_sync.cpp::handleRemoteFeedingCommands()`
- **Déclencheurs:**
  - Commande `"bouffePetits"` ou GPIO 108
  - Commande `"bouffeGros"` ou GPIO 109
- **Condition:** `_emailEnabled == true`
- **Méthode:** `mailer.send()`

### 4. Alertes Niveaux d'Eau
- **Localisation:** `src/automatism/automatism_alert_controller.cpp`
- **Déclencheurs:**
  - Niveau aquarium bas (`wlAqua > aqThresholdCm`)
  - Trop-plein détecté
- **Condition:** `mailNotif == true`
- **Méthode:** `mailer.sendAlert()`

### 5. Sleep/Wake
- **Localisation:** `src/automatism/automatism_sleep.cpp`
- **Déclencheurs:**
  - Entrée en sleep
  - Réveil du sleep
- **Condition:** `isEmailEnabled() == true`
- **Méthode:** `mailer.sendSleepMail()` / `mailer.sendWakeMail()`

### 6. OTA Updates
- **Localisation:** `src/bootstrap_network.cpp` et `src/ota_manager.cpp`
- **Déclencheurs:**
  - Début de mise à jour OTA
  - Fin de mise à jour OTA (succès/échec)
- **Condition:** `isEmailEnabled() == true`
- **Méthode:** `mailer.sendAlert()`

---

## 🔄 Traitement des Mails

### Queue Asynchrone
- **Type:** FreeRTOS Queue (`QueueHandle_t`)
- **Initialisation:** `mailer.initMailQueue()` au boot
- **Taille:** Non spécifiée dans le code visible
- **Traitement:** Séquentiel depuis `automationTask` (pas de tâche dédiée)

### Méthodes d'Envoi
1. **Asynchrone (recommandé):**
   - `mailer.send()` - Mail standard
   - `mailer.sendAlert()` - Alerte avec rapport détaillé
   - Ajoute à la queue et retourne immédiatement

2. **Synchrone (fallback):**
   - `mailer.sendSync()` - Envoi bloquant
   - Utilisé si queue non initialisée ou en cas d'erreur

### Traitement dans automationTask
- **Localisation:** `src/app_tasks.cpp` (lignes 345-353)
- **Priorité:** 2 (après heartbeat)
- **Condition:** `hasPendingMails() && TLSMutex::canConnect()`
- **Méthode:** `processOneMailSync()` - Traite UN mail à la fois
- **Protection:** Mutex TLS pour éviter collisions SMTP/HTTPS

---

## ⚠️ Conditions de Blocage

### Vérifications Préalables
1. **Email activé:** `isEmailEnabled() == true`
2. **Adresse valide:** `getEmailAddress()` non vide et ≠ "Non configuré"
3. **Heap suffisant:** `ESP.getFreeHeap() >= emailMinHeapBytes` (généralement 40-50 KB)
4. **WiFi connecté:** `WiFi.status() == WL_CONNECTED`
5. **Réseau prêt:** `waitForNetworkReadyForSMTP()` retourne true
6. **Mutex TLS disponible:** `TLSMutex::acquire()` réussit
7. **Pas de light sleep:** `g_enteringLightSleep == false`

### Erreurs Possibles
- **Queue pleine:** Mail ignoré (log: `"[Mail] ⚠️ Queue pleine, mail ignoré"`)
- **Heap insuffisant:** Envoi annulé
- **Mutex TLS indisponible:** Envoi annulé (timeout 10s)
- **Réseau non prêt:** Envoi annulé
- **Configuration SMTP invalide:** Initialisation échoue

---

## 🔍 Analyse du Monitoring

### Ce qui DEVRAIT apparaître dans les logs

#### Au Boot (si WiFi connecté):
```
[Mail] ===== INITIALISATION MAILER =====
[Mail] SMTP_HOST: '...'
[Mail] SMTP_PORT: ...
[Mail] ✅ Configuration SMTP prête
[Boot] Initialisation queue mail séquentielle...
[Boot] initMailQueue retourne: OK
=== TEST MAIL AU DÉMARRAGE ===
[Mail] ===== SENDALERT ASYNC =====
[Mail] 📥 Alerte ajoutée à la queue
```

#### Pendant le fonctionnement:
```
[Mail] 📥 Mail ajouté à la queue
[Mail] Traitement mail en cours...
[Mail] ✅ Mail envoyé avec succès
```

### Ce qui N'EST PAS apparu
- ❌ Aucune trace `[Mail]` dans le log
- ❌ Aucune initialisation mail
- ❌ Aucun envoi de mail
- ❌ Aucune erreur mail

---

## 🎯 Diagnostic

### Raisons Probables (par ordre de probabilité)

#### 1. **Mails désactivés dans la configuration runtime**
- **Probabilité:** 🔴 **ÉLEVÉE**
- **Cause:** `mailNotif = false` ou `_emailEnabled = false`
- **Vérification:** Vérifier la valeur dans NVS ou serveur distant
- **Solution:** Activer via interface web ou serveur distant

#### 2. **Adresse email non configurée**
- **Probabilité:** 🟡 **MOYENNE**
- **Cause:** `_emailAddress` vide ou "Non configuré"
- **Vérification:** `getEmailAddress()` retourne vide
- **Solution:** Configurer l'adresse email via interface web

#### 3. **WiFi non connecté au boot**
- **Probabilité:** 🟢 **FAIBLE** (WiFi était connecté pendant le monitoring)
- **Cause:** WiFi non connecté lors de l'appel à `onWifiReady()`
- **Vérification:** Vérifier les logs de boot complets
- **Solution:** S'assurer que WiFi se connecte avant l'initialisation mail

#### 4. **Aucun événement déclencheur**
- **Probabilité:** 🟡 **MOYENNE**
- **Cause:** Aucun nourrissage, alerte, ou événement système pendant 15 min
- **Vérification:** Normal si pas d'événement
- **Solution:** Déclencher manuellement un événement (nourrissage, alerte)

#### 5. **Initialisation mail échouée silencieusement**
- **Probabilité:** 🟢 **FAIBLE**
- **Cause:** `mailer.begin()` ou `initMailQueue()` échoue
- **Vérification:** Vérifier les logs de boot complets
- **Solution:** Vérifier la configuration SMTP (host, port, credentials)

---

## 📊 Points de Vérification

### À Vérifier dans les Logs de Boot Complets
1. ✅ Présence de `[Mail] ===== INITIALISATION MAILER =====`
2. ✅ Présence de `[Boot] Initialisation queue mail séquentielle...`
3. ✅ Présence de `=== TEST MAIL AU DÉMARRAGE ===`
4. ✅ Valeur de `initMailQueue retourne: OK` ou `ECHEC`

### À Vérifier dans la Configuration Runtime
1. ✅ `isEmailEnabled()` retourne `true`
2. ✅ `getEmailAddress()` retourne une adresse valide (non vide, ≠ "Non configuré")
3. ✅ Configuration SMTP valide (host, port, credentials)

### À Vérifier dans NVS
1. ✅ Clé `"gpio_email"` dans namespace `CONFIG` contient une adresse valide
2. ✅ Clé `"mailNotif"` dans namespace approprié contient `true`

---

## 🛠️ Recommandations

### Pour Activer les Mails

1. **Via Interface Web:**
   - Accéder à la page de configuration
   - Activer "Notifications email"
   - Configurer l'adresse email destinataire
   - Sauvegarder

2. **Via Serveur Distant:**
   - Envoyer configuration JSON avec:
     ```json
     {
       "mail": "votre@email.com",
       "mailNotif": "1"
     }
     ```

3. **Vérification:**
   - Redémarrer l'ESP32
   - Vérifier les logs de boot pour l'initialisation mail
   - Déclencher un événement (nourrissage manuel, alerte)
   - Vérifier la réception du mail

### Pour Tester le Système de Mail

1. **Test manuel via interface web:**
   - Déclencher un nourrissage manuel
   - Vérifier les logs `[Mail]`

2. **Test automatique:**
   - Attendre un nourrissage automatique (matin/midi/soir)
   - Vérifier la réception du mail

3. **Test d'alerte:**
   - Simuler un niveau d'eau bas
   - Vérifier l'envoi d'alerte

---

## ✅ Conclusion

Le système de mail est **correctement configuré et compilé** dans le code. Cependant, **aucune activité mail n'a été détectée** pendant le monitoring, ce qui suggère que:

1. **Les mails sont probablement désactivés** dans la configuration runtime (`mailNotif = false`)
2. **OU l'adresse email n'est pas configurée** (`_emailAddress` vide)
3. **OU aucun événement déclencheur n'est survenu** pendant le monitoring

**Action recommandée:** Vérifier la configuration runtime (activation + adresse email) et déclencher un événement de test pour valider le fonctionnement.

---

*Rapport généré automatiquement le 25 janvier 2026*
