# Analyse Monitoring WROOM-TEST (3 minutes)
**Date:** 2025-10-08 11:23-11:26
**Durée:** 180 secondes
**Version firmware:** 10.92
**Environnement:** TEST (PROFILE_TEST)

## ✅ Points Positifs

### 1. Système Opérationnel
- **Démarrage:** Réussi sans erreur critique
- **Uptime:** Système stable (560+ secondes depuis le boot précédent)
- **Reboots:** 35 redémarrages enregistrés (historique)
- **Heap:** Stable entre 57-70 KB (pas de fuite mémoire détectée)
- **Heap minimum:** 3132 bytes (marge correcte)

### 2. Connectivité WiFi
- **État:** Connecté et stable
- **RSSI:** -61 à -62 dBm (signal Acceptable)
- **Stabilité:** Aucune déconnexion pendant le monitoring
- **Checks WiFi:** Effectués régulièrement toutes les ~30s

### 3. Interface Web Locale
- **État:** ✅ Fonctionnelle
- **Client:** 192.168.0.158 (probablement votre navigateur)
- **Endpoint `/json`:** Répond correctement avec données capteurs
- **Fréquence:** Rafraîchissement régulier ~10s
- **Performance:** Temps de réponse corrects

### 4. Capteurs
- **Température eau:** 29.8°C (Dallas DS18B20 fonctionnel)
- **Ultrasoniques:** 
  - Potager: 171-174 cm
  - Aquarium: 209 cm
  - Réservoir: 173-209 cm (quelques sauts détectés)
- **Luminosité:** 1472 (fonctionnel)

### 5. Version Confirmée
```
sensor=esp32-wroom&version=10.92
```
✅ La version 10.92 est correctement déployée

## 🚨 Problèmes Critiques Identifiés

### PROBLÈME #1: Endpoints Serveur Distants Retournent 404

#### GET Remote State (États GPIO)
```
[HTTP] → GET remote state
[Web] GET remote state -> HTTP 404
[HTTP] ← 4294967295 bytes
[Web] JSON parse error: InvalidInput
```

**Fréquence:** Toutes les ~11 secondes (intervalle de polling)
**Impact:** ESP32 ne peut pas récupérer les commandes GPIO distantes
**Occurrences:** 33+ fois en 3 minutes

**URL utilisée (déduite du code):**
```
http://iot.olution.info/ffp3/ffp3datas/public/api/outputs-test/states/1
```

#### POST Sensor Data (Données capteurs)
```
[HTTP] → http://iot.olution.info/ffp3/ffp3datas/public/post-data-test (493 bytes)
[HTTP] ← code 404, 2157 bytes
```

**Fréquence:** Toutes les ~30-60 secondes (envoi données)
**Impact:** Les données capteurs ne sont PAS enregistrées dans la base de données
**Page 404:** Le serveur retourne une page HTML 404 complète

**Payload envoyé:**
```
api_key=fdGTMoptd5CD2ert3&
sensor=esp32-wroom&
version=10.92&
TempAir=nan&
Humidite=nan&
TempEau=29.8&
EauPotager=171&
EauAquarium=209&
EauReserve=173&
diffMaree=0&
Luminosite=1472&
etatPompeAqua=1&
etatPompeTank=0&
etatHeat=0&
etatUV=0&
bouffeMatin=8&
bouffeMidi=12&
bouffeSoir=19&
...
```

### PROBLÈME #2: Heartbeat Fonctionne (Anomalie)

```
[HTTP] → http://iot.olution.info/ffp3/ffp3datas/heartbeat.php (54 bytes)
[HTTP] ← code 200, 3 bytes
[HTTP] response: OK
```

**Observation:** Le heartbeat (ancien endpoint PHP) fonctionne parfaitement avec code 200 OK
**Conclusion:** Le serveur répond bien, mais les nouveaux endpoints Slim ne sont pas accessibles

### PROBLÈME #3: Capteur DHT (Température Air/Humidité)

```
[AirSensor] Échec de toutes les tentatives de récupération
[SystemSensors] Température air invalide finale: nan°C, force NaN
[SystemSensors] Humidité invalide finale: nan%, force NaN
[AirSensor] Capteur DHT non détecté ou déconnecté
```

**Impact:** Température air et humidité indisponibles (nan)
**Cause possible:** 
- Capteur DHT22/DHT11 déconnecté ou défaillant
- Problème de câblage
- Broche GPIO incorrecte

## 🔍 Analyse Détaillée

### Comparaison Endpoints

| Endpoint | Type | URL | Code HTTP | Status |
|----------|------|-----|-----------|--------|
| Heartbeat (legacy) | POST | `/ffp3/ffp3datas/heartbeat.php` | **200 OK** | ✅ Fonctionne |
| POST data TEST | POST | `/ffp3/ffp3datas/public/post-data-test` | **404** | ❌ Inaccessible |
| GET outputs TEST | GET | `/ffp3/ffp3datas/public/api/outputs-test/states/1` | **404** | ❌ Inaccessible |

### Hypothèses sur les 404

#### Hypothèse 1: Problème de Routing Slim
Le serveur FFP3 utilise Slim Framework avec un fichier `index.php` qui gère le routing. Les endpoints modernes sont définis dans ce fichier.

**Causes possibles:**
1. Le dossier `/public` n'est pas configuré comme racine web sur le serveur
2. `.htaccess` ou rewrite rules manquantes/incorrectes
3. Slim ne reçoit pas les requêtes (pas de mod_rewrite)
4. Le base path de Slim est incorrectement configuré

#### Hypothèse 2: Structure de Répertoires
L'URL actuelle est:
```
http://iot.olution.info/ffp3/ffp3datas/public/post-data-test
```

Mais peut-être que la structure devrait être:
```
http://iot.olution.info/ffp3/ffp3datas/post-data-test
(sans le /public/)
```

OU

```
http://iot.olution.info/ffp3/public/post-data-test
(public au niveau ffp3)
```

#### Hypothèse 3: Endpoints Non Déployés
Les fichiers modifiés dans le dossier `ffp3/` local n'ont peut-être pas été synchronisés avec le serveur distant `iot.olution.info`.

## 📋 Actions Correctives Requises

### Action #1: Vérifier Configuration Serveur Web
```bash
# Sur le serveur iot.olution.info
# Vérifier si mod_rewrite est activé
a2enmod rewrite

# Vérifier le DocumentRoot pour ffp3datas
cat /etc/apache2/sites-available/iot.olution.info.conf

# Vérifier .htaccess dans ffp3/ffp3datas/public/
cat /var/www/html/ffp3/ffp3datas/public/.htaccess
```

### Action #2: Vérifier Déploiement Fichiers
```bash
# Vérifier que index.php existe
ls -la /var/www/html/ffp3/ffp3datas/public/index.php

# Vérifier les permissions
ls -la /var/www/html/ffp3/ffp3datas/public/

# Tester manuellement l'endpoint
curl -X POST "http://iot.olution.info/ffp3/ffp3datas/public/post-data-test" \
  -d "api_key=fdGTMoptd5CD2ert3&sensor=test&version=10.92"
```

### Action #3: Créer .htaccess si Manquant
**Fichier:** `/var/www/html/ffp3/ffp3datas/public/.htaccess`
```apache
RewriteEngine On
RewriteCond %{REQUEST_FILENAME} !-f
RewriteCond %{REQUEST_FILENAME} !-d
RewriteRule ^ index.php [QSA,L]
```

### Action #4: Alternative - Utiliser Anciens Endpoints (Temporaire)
Si les nouveaux endpoints ne peuvent pas être fixés rapidement, modifier `project_config.h`:

```cpp
// Fallback vers anciens endpoints legacy
#if defined(PROFILE_TEST) || defined(PROFILE_DEV)
    constexpr const char* POST_DATA_ENDPOINT = "/ffp3/ffp3datas/post-ffp3-data2.php";
    constexpr const char* OUTPUT_ENDPOINT = "/ffp3/ffp3control/ffp3-outputs-action2.php?action=outputs_state&board=1";
#else
    constexpr const char* POST_DATA_ENDPOINT = "/ffp3/ffp3datas/post-ffp3-data.php";
    constexpr const char* OUTPUT_ENDPOINT = "/ffp3/ffp3control/ffp3-outputs-action.php?action=outputs_state&board=1";
#endif
```

### Action #5: Réparer Capteur DHT
- Vérifier connexion physique du DHT22/DHT11
- Vérifier la broche GPIO configurée dans `pins.h`
- Tester avec un autre capteur si disponible

## 📊 Statistiques

### Requêtes HTTP (3 minutes)
- **GET remote state:** 33+ requêtes (toutes en échec 404)
- **POST sensor data:** 1+ requête visible (échec 404)
- **Heartbeat:** 6+ requêtes (toutes réussies 200 OK)
- **Interface web locale (/json):** 20+ requêtes (toutes réussies)

### Performance Mémoire
- **Heap libre:** 57,020 - 70,188 bytes
- **Heap minimum:** 3,132 bytes
- **Tendance:** Stable (pas de fuite détectée)

### État Actionneurs
- **Pompe Aquarium:** ON (etatPompeAqua=1)
- **Pompe Réservoir:** OFF (etatPompeTank=0, verrouillée par sécurité)
- **Chauffage:** OFF (etatHeat=0)
- **Lumière UV:** OFF (etatUV=0)

## 🎯 Conclusion

### ✅ Ce qui fonctionne
1. Firmware v10.92 déployé et opérationnel
2. WiFi stable et connecté
3. Interface web locale accessible et fonctionnelle
4. Heartbeat vers serveur distant (ancien endpoint)
5. Capteurs principaux (eau, ultrasoniques, luminosité)
6. Système stable sans crashs

### ❌ Ce qui ne fonctionne pas
1. **CRITIQUE:** Endpoints modernes Slim retournent 404
   - POST données capteurs ne s'enregistrent PAS en BDD
   - GET commandes GPIO distantes ne fonctionnent PAS
2. Capteur DHT (température air/humidité) déconnecté

### 🔧 Prochaine Étape Immédiate
**Vérifier la configuration du serveur web distant `iot.olution.info`:**
1. Accéder au serveur via SSH
2. Vérifier que les fichiers sont déployés dans `/ffp3/ffp3datas/public/`
3. Vérifier `.htaccess` et mod_rewrite
4. Tester manuellement les endpoints avec curl
5. Regarder les logs Apache pour comprendre pourquoi les 404

**Alternative rapide:** Revenir temporairement aux anciens endpoints PHP legacy qui fonctionnent, le temps de déboguer le problème Slim.

