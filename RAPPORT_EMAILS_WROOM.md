# 📧 RAPPORT COMPLET - Système d'envoi d'emails (WROOM-TEST & WROOM-PROD)

**Date**: 2025-10-14  
**Environnements**: `wroom-test`, `wroom-prod`  
**Version analysée**: 10.11+  
**Statut**: ✅ FEATURE_MAIL activé dans les deux environnements

---

## 📋 TABLE DES MATIÈRES

1. [Conditions globales d'envoi](#conditions-globales-denvoi)
2. [Emails au démarrage](#emails-au-démarrage)
3. [Emails de nourrissage](#emails-de-nourrissage)
4. [Emails d'alertes capteurs](#emails-dalertes-capteurs)
5. [Emails de gestion des pompes](#emails-de-gestion-des-pompes)
6. [Emails de chauffage](#emails-de-chauffage)
7. [Emails de mise en veille/réveil](#emails-de-mise-en-veille-réveil)
8. [Emails OTA](#emails-ota)
9. [Emails manuels](#emails-manuels)
10. [Email digest périodique](#email-digest-périodique)
11. [Récapitulatif des fréquences](#récapitulatif-des-fréquences)
12. [Configuration SMTP](#configuration-smtp)

---

## 🔒 CONDITIONS GLOBALES D'ENVOI

**Pour qu'UN EMAIL SOIT ENVOYÉ**, les conditions suivantes doivent être remplies :

### ✅ Conditions de compilation
```cpp
#define FEATURE_MAIL 1  // Défini dans platformio.ini (ligne 67 wroom-prod, ligne 116 wroom-test)
```
- ✅ **wroom-test** : Activé
- ✅ **wroom-prod** : Activé

### ✅ Conditions runtime (CUMULATIVES)

1. **WiFi connecté** :
   ```cpp
   WiFi.status() == WL_CONNECTED
   ```

2. **Notifications emails activées** :
   ```cpp
   autoCtrl.isEmailEnabled() == true
   ```
   - Contrôlé par la variable serveur distant `mailNotif`
   - Modifiable via l'interface web (toggle)
   - Persisté dans la NVS

3. **Mémoire suffisante** (pour certains emails) :
   ```cpp
   ESP.getFreeHeap() >= 45000  // 45 KB minimum
   ```
   - Obligatoire pour : emails de veille, emails de réveil
   - Optionnel pour : autres emails (mais recommandé > 40KB)

4. **Adresse email configurée** :
   ```cpp
   autoCtrl.getEmailAddress() != ""
   ```
   - Variable serveur distant : `mail` ou `emailAddress`
   - Si vide, utilise `Config::DEFAULT_MAIL_TO` (arnould.svt@gmail.com)

---

## 📬 1. EMAILS AU DÉMARRAGE

### 1.1. Email de test au boot

**Fichier** : `src/app.cpp:644-741`

**Déclencheur** : Démarrage du système (une seule fois)

**Conditions** :
```cpp
#if FEATURE_MAIL
if (WiFi.status() == WL_CONNECTED && autoCtrl.isEmailEnabled()) {
```

**Sujet** :
```
FFP3CS - Test de démarrage [HOSTNAME]
```

**Contenu** :
- Version du firmware
- Date/heure de compilation
- Informations système (heap, flash, etc.)
- État des capteurs au démarrage
- État des actionneurs
- Configuration réseau

**Fréquence** : **Une fois au boot**

**Log associé** :
```
[App] Envoi du mail de test au démarrage...
[Mail] Message envoyé avec succès ✔
```

---

## 🍽️ 2. EMAILS DE NOURRISSAGE

### 2.1. Nourrissage automatique MATIN

**Fichier** : `src/automatism.cpp:990-997`

**Déclencheur** : Horloge programmée (par défaut 01:00)

**Conditions** :
```cpp
if (emailEnabled) {
```

**Sujet** :
```
FFP3 - Bouffe matin
```

**Contenu** :
- Heure de nourrissage
- Durée gros poissons
- Durée petits poissons
- État des pompes
- Niveaux d'eau (aquarium, réserve)
- Température eau

**Fréquence** : **Une fois par jour à l'heure configurée**

**Variables configurables** :
- `feedMorning` (heure, défaut : 1)
- `tempsGros` / `feedBigDur` (durée gros poissons, secondes)
- `tempsPetits` / `feedSmallDur` (durée petits poissons, secondes)

---

### 2.2. Nourrissage automatique MIDI

**Fichier** : `src/automatism.cpp:1044-1051`

**Déclencheur** : Horloge programmée (par défaut 13:00)

**Conditions** : Identiques au matin

**Sujet** :
```
FFP3 - Bouffe midi
```

**Fréquence** : **Une fois par jour à l'heure configurée**

---

### 2.3. Nourrissage automatique SOIR

**Fichier** : `src/automatism/automatism_feeding.cpp:280-284`

**Déclencheur** : Horloge programmée (par défaut 19:00)

**Conditions** : Identiques au matin

**Sujet** :
```
FFP3 - Bouffe soir
```

**Particularité** : **Petits poissons UNIQUEMENT** (pas de gros le soir)

**Fréquence** : **Une fois par jour à l'heure configurée**

---

### 2.4. Nourrissage MANUEL (petits)

**Fichier** : `src/web_server.cpp:446-450`

**Déclencheur** : Action utilisateur (interface web ou API)

**Endpoint** : `POST /api/control?c=small`

**Conditions** :
```cpp
if (autoCtrl.isEmailEnabled()) {
```

**Sujet** :
```
FFP3 - Bouffe manuelle
```

**Contenu** : "Bouffe manuelle - Petits poissons"

**Fréquence** : **À la demande** (pas de limite)

---

### 2.5. Nourrissage MANUEL (gros)

**Fichier** : `src/web_server.cpp:468-472`

**Déclencheur** : Action utilisateur (interface web ou API)

**Endpoint** : `POST /api/control?c=big`

**Conditions** : Identiques au nourrissage manuel petits

**Sujet** :
```
FFP3 - Bouffe manuelle
```

**Contenu** : "Bouffe manuelle - Gros poissons"

**Fréquence** : **À la demande**

---

## 🌊 3. EMAILS D'ALERTES CAPTEURS

### 3.1. Alerte - Niveau aquarium BAS

**Fichier** : `src/automatism.cpp:1124-1129`

**Déclencheur** : Distance capteur aquarium > seuil configuré

**Conditions** :
```cpp
if (r.wlAqua > aqThresholdCm && !lowAquaSent && emailEnabled) {
```

**Sujet** :
```
FFP3 - Alerte - Niveau aquarium BAS
```

**Contenu** :
```
Distance: XX cm (> YY cm)
```

**Anti-spam** :
- Flag `lowAquaSent` : empêche envoi multiple
- Reset après hystérésis (-5 cm)

**Fréquence** : **Une fois par événement** (avec hystérésis)

**Seuil configurable** : `aqThreshold` (défaut : 150 cm pour WROOM)

---

### 3.2. Alerte - Aquarium TROP PLEIN

**Fichier** : `src/automatism.cpp:1151-1162`

**Déclencheur** : Distance capteur aquarium < limite flood

**Conditions** :
```cpp
if (!inFlood && emailEnabled) {
    bool debounceOk = elapsedUnder >= (floodDebounceMin * 60UL);
    bool cooldownOk = (lastFloodEmailEpoch == 0) || 
                      ((nowEpoch - lastFloodEmailEpoch) >= (floodCooldownMin * 60UL));
    if (debounceOk && cooldownOk) {
```

**Sujet** :
```
FFP3 - Alerte - Aquarium TROP PLEIN
```

**Contenu** :
```
Distance: XX cm (< YY cm)
[/ Pompe verrouillée]  (si pompe bloquée)
```

**Anti-spam RENFORCÉ** :
- **Debounce** : 2 minutes minimum sous le seuil
- **Cooldown** : 30 minutes minimum entre deux envois
- **Persistance** : Cooldown survit aux reboots (NVS)
- **Hystérésis** : 5 cm pour sortie d'alerte
- **Stabilité temporelle** : 3 minutes au-dessus du seuil pour reset

**Fréquence** : **Maximum 1 email toutes les 30 minutes**

**Seuils configurables** :
- `limFlood` (défaut : 30 cm)
- `floodDebounceMin` (débounce : 2 min)
- `floodCooldownMin` (cooldown : 30 min)
- `floodHystCm` (hystérésis : 5 cm)
- `floodResetStableMin` (stabilité : 3 min)

---

### 3.3. Alerte - Réserve BASSE

**Fichier** : `src/automatism.cpp:1187-1192`

**Déclencheur** : Distance capteur réserve > seuil

**Conditions** :
```cpp
if (r.wlTank > tankThresholdCm && !lowTankSent && emailEnabled) {
```

**Sujet** :
```
FFP3 - Alerte - Réserve BASSE
```

**Contenu** :
```
Distance: XX cm (> YY cm)
```

**Anti-spam** : Flag `lowTankSent`

**Fréquence** : **Une fois par événement**

**Seuil configurable** : `tankThreshold` (défaut : 150 cm)

---

### 3.4. Info - Réserve OK

**Fichier** : `src/automatism.cpp:1195-1200`

**Déclencheur** : Distance capteur réserve retourne sous le seuil

**Conditions** :
```cpp
else if (lowTankSent && r.wlTank <= tankThresholdCm - 5 && emailEnabled) {
```

**Sujet** :
```
FFP3 - Info - Réserve OK
```

**Contenu** :
```
Distance: XX cm (<= YY cm)
```

**Fréquence** : **Une fois par retour à la normale**

---

## 💧 4. EMAILS DE GESTION DES POMPES

### 4.1. Pompe réservoir - Changements d'état (AUTO/MANUEL)

**Fichier** : `src/automatism.cpp:1688-1815`

**Déclencheurs** :
- Démarrage pompe réservoir (manuel ou auto)
- Arrêt pompe réservoir (manuel ou auto)

**Conditions** :
```cpp
if (emailEnabled && !isSecurityStop) {
    // Envoi seulement si changement réel (anti-doublon)
```

**Sujets possibles** :
```
Pompe réservoir MANUELLE ON
Pompe réservoir AUTO ON
Pompe réservoir MANUELLE OFF
Pompe réservoir AUTO OFF
```

**Contenu** :
- Mode (manuel/auto)
- État pompe
- Niveaux aquarium et réserve
- Durée de fonctionnement (si arrêt)
- Raison de l'arrêt (si applicable)

**Anti-doublon** :
- Flags `emailTankStartSent` et `emailTankStopSent`
- Reset mutuel à chaque changement

**Fréquence** : **À chaque changement d'état** (ON→OFF ou OFF→ON)

---

### 4.2. Pompe réservoir - Arrêt SÉCURITÉ

**Fichier** : `src/automatism.cpp:1712-1720`

**Déclencheurs** :
- Réserve trop basse (sécurité)
- Aquarium trop plein (sécurité)

**Sujets possibles** :
```
Pompe réservoir OFF (sécurité réserve basse)
Pompe réservoir OFF (sécurité aquarium trop plein)
Pompe réservoir OFF (sécurité)
```

**Conditions** : Envoi TOUJOURS (même si emails désactivés pour les alertes normales)

**Fréquence** : **À chaque arrêt de sécurité**

---

### 4.3. Pompe réservoir BLOQUÉE (réserve basse)

**Fichier** : `src/automatism.cpp:682-686`

**Déclencheur** : Pompe réservoir ne peut pas démarrer (réserve insuffisante)

**Conditions** :
```cpp
if (emailEnabled && !emailTankSent) {
```

**Sujet** :
```
FFP3 - Pompe réservoir BLOQUÉE (réserve basse)
```

**Contenu** :
- Raison du blocage
- Niveaux actuels (aquarium, réserve)
- Seuils configurés
- Condition de déblocage

**Anti-spam** : Flag `emailTankSent`

**Fréquence** : **Une fois par blocage**

---

### 4.4. Pompe réservoir bloquée (timeout)

**Fichier** : `src/automatism.cpp:778-782`

**Déclencheur** : Timeout de la pompe réservoir (blocage mécanique)

**Conditions** :
```cpp
if (emailEnabled && !emailTankSent) {
```

**Sujet** :
```
FFP3 - Pompe réservoir bloquée
```

**Contenu** :
- Description du problème (timeout)
- Actions de diagnostic suggérées
- Procédure de déblocage

**Fréquence** : **Une fois par incident**

---

### 4.5. Pompe réservoir verrouillée (réserve trop basse)

**Fichier** : `src/automatism.cpp:854-858`

**Déclencheur** : Verrouillage permanent pour protection

**Conditions** :
```cpp
if (emailEnabled && !emailTankSent) {
```

**Sujet** :
```
FFP3 - Pompe réservoir verrouillée (réserve trop basse)
```

**Contenu** :
- Raison du verrouillage
- Niveaux critiques
- Procédure de déblocage manuel
- Conseils (remplir réserve, éviter marche à sec)

**Fréquence** : **Une fois par verrouillage**

---

### 4.6. Pompe aquarium - Changements d'état

**Fichier** : `src/automatism.cpp:1813-1816` + `1864-1867`

**Déclencheurs** :
- Démarrage pompe aquarium
- Arrêt pompe aquarium

**Conditions** :
```cpp
if (emailEnabled) {
```

**Sujets possibles** :
```
Pompe AQUARIUM ON (AUTO)
Pompe AQUARIUM OFF (AUTO)
Pompe AQUARIUM OFF (MANUEL)
Pompe AQUARIUM ON (MANUEL)
```

**Contenu** :
- Mode (auto/manuel)
- État pompe
- Niveaux d'eau
- Seuils configurés

**Fréquence** : **À chaque changement d'état**

---

## 🌡️ 5. EMAILS DE CHAUFFAGE

### 5.1. Chauffage ON

**Fichier** : `src/automatism.cpp:1206-1210`

**Déclencheur** : Température eau < seuil

**Conditions** :
```cpp
if (emailEnabled) {
```

**Sujet** :
```
FFP3 - Chauffage ON
```

**Contenu** :
```
Temp eau: XX.X°C
```

**Fréquence** : **À chaque activation**

**Seuil configurable** : `heaterThreshold` (défaut : 23.0°C)

---

### 5.2. Chauffage OFF

**Fichier** : `src/automatism.cpp:1214-1218`

**Déclencheur** : Température eau ≥ seuil

**Conditions** : Identiques au chauffage ON

**Sujet** :
```
FFP3 - Chauffage OFF
```

**Fréquence** : **À chaque désactivation**

---

## 😴 6. EMAILS DE MISE EN VEILLE / RÉVEIL

### 6.1. Email de mise en veille (SLEEP)

**Fichier** : `src/automatism.cpp:2332-2365`

**Déclencheur** : Entrée en light-sleep automatique

**Conditions** :
```cpp
#if FEATURE_MAIL
uint32_t heapBeforeMail = ESP.getFreeHeap();
const uint32_t MIN_HEAP_FOR_MAIL = 45000; // 45KB minimum

if (WiFi.status() == WL_CONNECTED && autoCtrl.isEmailEnabled()) {
    if (heapBeforeMail >= MIN_HEAP_FOR_MAIL) {
```

**Sujet** :
```
FFP3 - Mise en veille
```

**Contenu** :
- Raison de la mise en veille
- Durée de veille prévue (en secondes)
- Timestamp
- État du système (températures, niveaux, actionneurs)
- Configuration sleep
- **IMPORTANT** : Utilise `_lastReadings` (pas de lecture capteurs)

**Skip si** : Heap < 45 KB

**Log si skip** :
```
[Auto] ⚠️ Skip mail de mise en veille: heap insuffisant (XXXXX < 45000 bytes)
```

**Fréquence** : **À chaque entrée en sleep** (~toutes les 150 secondes en fonctionnement normal)

**Optimisation v11.06** :
- ✅ Pas de lecture capteurs (évite blocage DHT22)
- ✅ Temps d'envoi : 2-3s (au lieu de 15-20s)

---

### 6.2. Email de réveil (WAKE)

**Fichier** : `src/automatism.cpp:2407-2428`

**Déclencheur** : Réveil du light-sleep

**Conditions** : Identiques au mail de veille

**Sujet** :
```
FFP3 - Réveil du système
```

**Contenu** :
- Raison du réveil
- Durée réelle de veille (en secondes)
- Timestamp
- État du système au réveil
- Configuration sleep
- Informations réseau (WiFi, IP, RSSI)
- **IMPORTANT** : Utilise `_lastReadings`

**Skip si** : Heap < 45 KB

**Fréquence** : **À chaque réveil** (~toutes les 1200 secondes = 20 minutes)

**Note** : La fréquence peut varier selon :
- `FreqWakeUp` (configuration serveur, défaut : 1200s)
- Mode nuit : multiplicateur ×2 (2400s)

---

## 🔄 7. EMAILS OTA (Mise à jour firmware)

### 7.1. OTA - Début (Serveur distant)

**Fichier** : `src/ota_manager.cpp:780-801`

**Déclencheur** : Début de mise à jour OTA automatique depuis serveur distant

**Conditions** : Aucune condition email (toujours envoyé si disponible)

**Destination** :
- Si `emailEnabled`: adresse configurée
- Sinon : `Config::DEFAULT_MAIL_TO`

**Sujet** :
```
FFP3 - OTA début - Serveur distant
```

**Contenu** :
- Méthode : Tâche dédiée HTTP OTA
- Version courante
- Version distante
- URL firmware
- Taille firmware
- MD5 firmware
- URL filesystem (si applicable)
- Partition courante

**Fréquence** : **À chaque début d'OTA automatique**

---

### 7.2. OTA - Succès (Serveur distant)

**Fichier** : `src/app.cpp:780-804`

**Déclencheur** : Redémarrage après OTA réussie (serveur distant)

**Conditions** :
```cpp
if (g_otaJustUpdated && autoCtrl.isEmailEnabled()) {
```

**Sujet** :
```
OTA mise à jour - Serveur distant [HOSTNAME]
```

**Contenu** :
- Méthode : Serveur distant automatique
- Ancienne version
- Nouvelle version
- Hostname
- Date/heure compilation
- Confirmation redémarrage

**Fréquence** : **Une fois après chaque OTA réussie**

---

### 7.3. OTA - Début (Interface web)

**Fichier** : `src/app.cpp:575-586`

**Déclencheur** : Début de mise à jour via interface web (ArduinoOTA)

**Conditions** : Aucune (toujours envoyé)

**Destination** :
- Si `emailEnabled`: adresse configurée
- Sinon : `Config::DEFAULT_MAIL_TO`

**Sujet** :
```
OTA début - Interface web
```

**Contenu** :
- Méthode : Interface web ArduinoOTA
- Version actuelle
- Hostname
- Port OTA

**Fréquence** : **À chaque début d'OTA web**

---

### 7.4. OTA - Succès (Interface web)

**Fichier** : `src/app.cpp:808-833`

**Déclencheur** : Redémarrage après OTA réussie (interface web)

**Conditions** :
```cpp
if (config.getOtaUpdateFlag()) {
```

**Destination** :
- Si `emailEnabled`: adresse configurée
- Sinon : `Config::DEFAULT_MAIL_TO`

**Sujet** :
```
OTA mise à jour - Interface web [HOSTNAME]
```

**Contenu** :
- Méthode : Interface web ElegantOTA
- Nouvelle version
- Hostname
- Date/heure compilation
- Confirmation redémarrage

**Fréquence** : **Une fois après chaque OTA web réussie**

---

## 🧪 8. EMAILS MANUELS

### 8.1. Email de test (endpoint /mailtest)

**Fichier** : `src/web_server.cpp:1259-1270`

**Déclencheur** : Appel API manuel

**Endpoint** : `GET /mailtest?subject=XXX&body=YYY&to=ZZZ`

**Conditions** : Aucune (envoi direct)

**Paramètres** :
- `subject` : Sujet (optionnel, défaut : "Test email")
- `body` : Corps (optionnel, défaut : "Ceci est un e-mail de test...")
- `to` : Destinataire (optionnel, défaut : `Config::DEFAULT_MAIL_TO`)

**Sujet** :
```
FFP3 - [SUJET PERSONNALISÉ]
```

**Fréquence** : **À la demande** (pas de limite)

**Usage** : Tests, diagnostics, notifications personnalisées

---

### 8.2. Redémarrage GPIO

**Fichier** : `src/automatism.cpp:1474-1478`

**Déclencheur** : Reset système via GPIO 110

**Conditions** :
```cpp
if (emailEnabled) {
```

**Sujet** :
```
FFP3 - Redémarrage GPIO
```

**Contenu** :
```
Reset GPIO 110 activé – redémarrage en cours
```

**Fréquence** : **À chaque reset manuel**

---

## 📊 9. EMAIL DIGEST PÉRIODIQUE

### 9.1. Digest événements

**Fichier** : `src/app.cpp:922-969`

**Déclencheur** : Timer périodique

**Intervalle** : **24 heures** (défini par `DIGEST_INTERVAL_MS`)

**Conditions** :
```cpp
if (WiFi.status() == WL_CONNECTED && autoCtrl.isEmailEnabled()) {
```

**Sujet** :
```
FFP3CS - Digest événements [HOSTNAME]
```

**Contenu** :
- Résumé des événements récents (depuis dernier envoi)
- Informations système
- Marges de stack des tâches FreeRTOS
- Dérive temporelle (Time Drift Monitor)
- Événements du journal (EventLog)

**Particularité** :
- Envoi seulement si nouveaux événements
- Séquence d'événements persistée (NVS)
- Taille max : 5 KB (`EMAIL_DIGEST_MAX_SIZE_BYTES`)

**Fréquence** : **Maximum 1 fois par 24 heures** (si nouveaux événements)

**Log si aucun événement** :
```
[EventLog] Digest: no new events
```

---

## 📈 10. RÉCAPITULATIF DES FRÉQUENCES

### Emails à fréquence fixe

| Type d'email | Fréquence | Conditions |
|--------------|-----------|------------|
| **Test boot** | 1× au démarrage | WiFi + emailEnabled |
| **Nourrissage matin** | 1×/jour (01:00) | emailEnabled |
| **Nourrissage midi** | 1×/jour (13:00) | emailEnabled |
| **Nourrissage soir** | 1×/jour (19:00) | emailEnabled |
| **Digest périodique** | Max 1×/24h | WiFi + emailEnabled + nouveaux événements |
| **Sleep** | ~1×/150s | WiFi + emailEnabled + heap ≥ 45KB |
| **Wake** | ~1×/1200s (20 min) | WiFi + emailEnabled + heap ≥ 45KB |

### Emails déclenchés par événements

| Type d'email | Déclencheur | Anti-spam |
|--------------|-------------|-----------|
| **Aquarium BAS** | Distance > seuil | Flag + hystérésis -5cm |
| **Aquarium TROP PLEIN** | Distance < limFlood | Debounce 2min + Cooldown 30min |
| **Réserve BASSE** | Distance > seuil | Flag |
| **Réserve OK** | Retour sous seuil | Flag + hystérésis -5cm |
| **Pompe réservoir ON/OFF** | Changement état | Flags start/stop |
| **Pompe bloquée** | Timeout/Réserve vide | Flag `emailTankSent` |
| **Chauffage ON/OFF** | Seuil température | Par événement |
| **OTA** | Début + fin | Par mise à jour |
| **Nourrissage manuel** | Action utilisateur | Aucun (illimité) |

### Emails à la demande

| Type d'email | Fréquence |
|--------------|-----------|
| **Email test manuel** | Illimité |
| **Reset GPIO** | Par action |

---

## 🔧 11. CONFIGURATION SMTP

### Serveur SMTP

**Configuration** : `src/mailer.cpp:112-135`

```cpp
// SMTP Gmail
smtp.host = "smtp.gmail.com"
smtp.port = 465
smtp.secure = true (SSL)
```

### Authentification

**Credentials** : `include/secrets.h`

```cpp
constexpr const char* AUTHOR_EMAIL = "arnould.svt@gmail.com";
constexpr const char* AUTHOR_PASSWORD = "ddbfvlkssfleypdr";  // App Password
```

### Adresse destinataire

**Par défaut** : `include/config.h`

```cpp
constexpr const char* DEFAULT_MAIL_TO = "arnould.svt@gmail.com";
```

**Dynamique** : Configuration via serveur distant (variable `mail` ou `emailAddress`)

### Formats des emails

**Expéditeur** :
```
FFP3CS Boot <arnould.svt@gmail.com>
FFP3 Alert <arnould.svt@gmail.com>
FFP3 System <arnould.svt@gmail.com>
```

**Préfixe sujet** : Tous les sujets commencent par `FFP3` ou `FFP3CS`

---

## 📊 12. STATISTIQUES ET VOLUMÉTRIE

### Volume d'emails estimé (24 heures)

**Scénario nominal** (sans alertes) :

| Type | Fréquence/jour | Nombre |
|------|----------------|--------|
| Nourrissage automatique | 3×/jour | 3 |
| Sleep | ~576×/jour (150s) | 576 |
| Wake | ~72×/jour (1200s) | 72 |
| Digest | 1×/24h | 1 |
| **TOTAL** | | **652 emails/jour** |

**Scénario avec alertes modérées** :

| Type | Ajout estimé/jour |
|------|-------------------|
| Aquarium BAS | +2 |
| Aquarium TROP PLEIN | +1 (max 48 si 30min cooldown) |
| Réserve BASSE | +2 |
| Pompe réservoir ON/OFF | +10 |
| Chauffage ON/OFF | +20 |
| **TOTAL** | **~687 emails/jour** |

### Taille des emails

| Type d'email | Taille estimée |
|--------------|----------------|
| Sleep/Wake | 800-1200 bytes |
| Nourrissage | 600-800 bytes |
| Alertes simples | 200-400 bytes |
| OTA | 600-1000 bytes |
| Digest | 2000-5000 bytes (max) |
| Test boot | 1500-2500 bytes |

**Volume total quotidien** : ~400-500 KB

---

## ⚠️ 13. POINTS D'ATTENTION

### Limitations

1. **Quotas Gmail** :
   - Limite : 500 emails/jour pour un compte Gmail standard
   - **ATTENTION** : Le système peut dépasser cette limite avec les emails de sleep
   - **Solution** : Désactiver les emails de sleep en production si non critiques

2. **Mémoire heap** :
   - Emails skip si heap < 45 KB (sleep/wake)
   - Autres emails peuvent échouer si heap < 30 KB

3. **WiFi** :
   - Tous les emails nécessitent WiFi connecté
   - Aucun buffer/queue pour emails non envoyés
   - Si WiFi down → emails perdus

4. **SMTP timeout** :
   - Timeout envoi : 10 secondes par défaut
   - Si échec → log erreur, pas de retry

### Recommandations

#### Pour WROOM-PROD (Production)

**Désactiver ou réduire** :
```cpp
// Option 1: Désactiver les emails de sleep/wake
// Dans automatism.cpp, commenter les blocs sendSleepMail() et sendWakeMail()

// Option 2: Augmenter la fréquence de wake (réduire nombre d'emails)
freqWakeSec = 3600;  // 1 heure au lieu de 20 minutes
```

**Optimiser les alertes** :
- Cooldown TROP PLEIN : garder 30 minutes minimum
- Désactiver emails chauffage ON/OFF si non critiques

#### Pour WROOM-TEST (Test/Développement)

**Garder tous les emails activés** pour surveillance complète

---

## 📋 14. CHECKLIST DE VÉRIFICATION

### ✅ Configuration requise

- [ ] `FEATURE_MAIL=1` dans platformio.ini
- [ ] Bibliothèque `ESP Mail Client` installée
- [ ] WiFi configuré et connecté
- [ ] Variable `mailNotif=checked` dans serveur distant
- [ ] Variable `mail=<adresse>` configurée
- [ ] Credentials SMTP valides dans `secrets.h`
- [ ] Port 465 (SMTP SSL) ouvert sur le réseau

### ✅ Tests à effectuer

1. **Boot** :
   - [ ] Email de test reçu au démarrage
   - [ ] Log : `[Mail] Message envoyé avec succès ✔`

2. **Nourrissage** :
   - [ ] Email matin à l'heure configurée
   - [ ] Email midi à l'heure configurée
   - [ ] Email soir à l'heure configurée
   - [ ] Email manuel petits
   - [ ] Email manuel gros

3. **Alertes** :
   - [ ] Email aquarium BAS (distance > seuil)
   - [ ] Email aquarium TROP PLEIN (distance < limFlood)
   - [ ] Email réserve BASSE
   - [ ] Email chauffage ON/OFF

4. **Sleep/Wake** :
   - [ ] Email sleep après ~150s
   - [ ] Email wake après ~20 min
   - [ ] Vérifier heap ≥ 45KB dans logs

5. **OTA** :
   - [ ] Email début OTA
   - [ ] Email succès OTA après reboot

6. **Digest** :
   - [ ] Email digest après 24h (si événements)

---

## 🎯 15. RÉSUMÉ EXÉCUTIF

### Configuration actuelle (wroom-test & wroom-prod)

- ✅ **FEATURE_MAIL activé** dans les deux environnements
- ✅ **25+ types d'emails** différents
- ✅ **652+ emails/jour** en fonctionnement nominal
- ✅ **Anti-spam** robuste sur alertes critiques
- ✅ **Optimisations mémoire** (v11.06) pour sleep/wake

### Points forts

- ✅ Système complet de notifications
- ✅ Debounce et cooldown sur alertes flood
- ✅ Utilisation de cache pour emails sleep/wake (pas de blocage)
- ✅ Persistance cooldown (survit aux reboots)
- ✅ Flexibilité : emails manuel + auto

### Points d'amélioration potentiels

- ⚠️ Volume d'emails élevé (risque quota Gmail)
- ⚠️ Pas de queue/retry si envoi échoue
- ⚠️ Tous les emails nécessitent WiFi (aucun offline)
- 💡 **Suggestion** : Implémenter un système de priorités (critique/info/debug)

---

**Document généré le** : 2025-10-14  
**Version firmware analysée** : 10.11+  
**Environnements** : wroom-test, wroom-prod  
**Status** : ✅ Complet et à jour





