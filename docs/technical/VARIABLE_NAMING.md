# Convention de Nommage des Variables - FFP5CS

**Version:** 1.6  
**Date:** 2026-02-15  
**Statut:** Documentation de reference (alignement cles NVS avec include/nvs_keys.h ; gestion du temps §0)

---

## Sources de verite (contrat firmware / serveur)

Pour eviter les redondances et les desynchronisations, chaque cote du contrat doit avoir une source unique, tenue a jour a chaque changement de GPIO ou de cle.

| Cote | Fichier(s) source | Contenu |
|------|-------------------|--------|
| **Firmware** | `include/gpio_mapping.h` | GPIOMap (GPIO, serverPostName, internalName, nvsKey), SensorMap (capteurs), GPIODefaults |
| **Firmware** | `include/nvs_keys.h` | Cles NVS (namespaces Config, System, Diag, Automatism, Sync, WebClient) |
| **Serveur (ffp3)** | `src/Controller/OutputController.php` | `$parameterGpioMap` (GPIO pour page controle) |
| **Serveur (ffp3)** | `src/Repository/OutputRepository.php` | Mapping GPIO vers proprietes SensorData dans `syncStatesFromSensorData()` |
| **Serveur (ffp3)** | `src/Controller/PostDataController.php` + `src/Domain/SensorData.php` | Cles POST et parametres du DTO |

**Documentation API locale (serveur embarqué)** : liste des routes HTTP (port 80) et WebSocket (port 81, path `/ws`) dans `docs/technical/api-endpoints.yaml`.

**Procedure de synchronisation** (ajout ou modification d'un GPIO / d'une cle) :
1. Mettre a jour `include/gpio_mapping.h` (et `include/nvs_keys.h` si nouvelle cle NVS).
2. Mettre a jour les fichiers serveur concernes (OutputController, OutputRepository, PostDataController/SensorData si cle POST).
3. Mettre a jour le present document (tables et section 8) et les references croisees.
4. Tester firmware + serveur (POST/GET, page controle).

*Objectif a terme : centraliser le mapping GPIO cote serveur dans un seul fichier (ex. config ou classe dediee) derive par OutputController et OutputRepository pour reduire la redondance.*

---

## Vue d'ensemble

Ce document centralise les conventions de nommage entre les differentes couches du systeme FFP5CS :
- **Serveur distant** (reference absolue - iot.olution.info)
- **Firmware ESP32** (variables C++ internes)
- **NVS** (stockage persistant local)
- **API JSON locale** (endpoints HTTP locaux)

---

## 0. Gestion du temps (timezone, RTC, horaires)

### Firmware (ESP32)

- **RTC** : Heure systeme (epoch) initialisee par `loadTimeWithFallback()` : NVS `rtc_epoch` puis `EPOCH_COMPILE_TIME` puis `EPOCH_DEFAULT_FALLBACK`. Sync NTP quand WiFi disponible (`include/config.h` : `NTP_SERVER`, `NTP_GMT_OFFSET_SEC`, `NTP_DAYLIGHT_OFFSET_SEC`).
- **Timezone firmware** : Fixe **UTC+1** (pas d'heure d'ete), configure dans `src/power.cpp` via `setenv("TZ", "<+01>-1", 1)`. Choix volontaire pour simplicite et alignement approximatif avec Maroc/France hiver. En ete, Casablanca peut différer (DST Maroc) ; les horaires de nourrissage et la fenetre « nuit » (22h–6h) restent en heure locale ESP32.
- **Horaires nourrissage** : Heures matin/midi/soir en NVS (`gpio_feedMorn`, `gpio_feedNoon`, `gpio_feedEve`) et synchronises avec le serveur via les cles POST/GET `bouffeMatin`, `bouffeMidi`, `bouffeSoir` (voir §3). La logique metier utilise `getCurrentEpochSafe()` puis `localtime_r()` pour obtenir heure/minute/jour.
- **Timestamps d'etat** : `state_lastLocal`, `sync_lastSync` en NVS (millisecondes, type `unsigned long`) ; pas d'echange d'horodatage device dans le POST principal vers le serveur (option future : champ `device_time` pour diagnostic).

### Serveur (ffp3)

- **Stockage** : Tous les timestamps applicatifs (Boards.`last_request`, Outputs.`requestTime`, etc.) sont en **heure serveur** (`APP_TIMEZONE`, defaut `Europe/Paris`). MySQL utilise `NOW()` ; la session doit etre coherente avec le timezone PHP (voir `ffp3/docs/TIMEZONE_MANAGEMENT.md`).
- **Affichage** : Conversion vers `Africa/Casablanca` pour l'interface (moment-timezone, Highcharts). Voir doc timezone ffp3 pour details.

### Alignement firmware / serveur

- Les **horaires** (matin/midi/soir) sont harmonises (memes noms GET/POST). Le firmware est la source d'execution (NVS) ; le serveur met a jour la config locale via GET outputs/state.
- Aucune **heure device** n'est envoyee dans le POST post-data aujourd'hui ; les dates cote serveur sont l'heure de reception. Si signature HMAC est utilisee (`timestamp` + `signature`), la fenetre de validite (`SIG_VALID_WINDOW`, ex. 300 s) doit etre compatible avec la derive max du RTC ESP32 (NTP + correction de derive).

---

## 1. Variables Capteurs

| Description | Serveur POST | Serveur GET | Firmware C++ | API Locale `/json` | NVS |
|-------------|--------------|-------------|--------------|-------------------|-----|
| Temperature air | `TempAir` | - | `tempAir` | `tempAir` | - |
| Humidite | `Humidite` | - | `humidity` (SensorReadings) / `humid` (Measurements) | `humidity` | - |
| Temperature eau | `TempEau` | - | `tempWater` | `tempWater` | `temp_last_ok` (cfg) |
| Niveau potager | `EauPotager` | - | `wlPota` | `wlPota` | - |
| Niveau aquarium | `EauAquarium` | - | `wlAqua` | `wlAqua` | - |
| Niveau reservoir | `EauReserve` | - | `wlTank` | `wlTank` | - |
| Difference maree | `diffMaree` | - | `diffMaree` | - | - |
| Luminosite | `Luminosite` | - | `luminosite` | `luminosite` | - |

### Harmonisation effectuee (2026-01-31)

- **`humidity`** : La struct `Measurements` utilise maintenant `humidity` (comme `SensorReadings` et l'API)

---

## 2. Variables Actionneurs

| Description | Serveur POST | Serveur GET | Firmware C++ | API Locale `/json` | NVS |
|-------------|--------------|-------------|--------------|-------------------|-----|
| Pompe aquarium | `etatPompeAqua` | `"16"`, `pump_aqua` | `pumpAqua` | `pumpAqua` | - |
| Pompe reservoir | `etatPompeTank` | `"18"`, `pump_tank` | `pumpTank` | `pumpTank` | - |
| Chauffage | `etatHeat` | `"2"`, `heat` | `heater` | `heater` | - |
| Lumiere UV | `etatUV` | `"15"`, `light` | `light` | `light` | - |

---

## 3. Variables Configuration Nourrissage

| Description | Serveur POST | Serveur GET | Firmware C++ | API Locale `/dbvars` | NVS (cfg) |
|-------------|--------------|-------------|--------------|---------------------|-----------|
| Heure matin | `bouffeMatin` | `"105"`, `bouffeMatin` | `bouffeMatin` | `bouffeMatin` | dans `remote_json` |
| Heure midi | `bouffeMidi` | `"106"`, `bouffeMidi` | `bouffeMidi` | `bouffeMidi` | dans `remote_json` |
| Heure soir | `bouffeSoir` | `"107"`, `bouffeSoir` | `bouffeSoir` | `bouffeSoir` | dans `remote_json` |
| Duree gros | `tempsGros` | `"111"`, `tempsGros` | `tempsGros` | `tempsGros` | dans `remote_json` |
| Duree petits | `tempsPetits` | `"112"`, `tempsPetits` | `tempsPetits` | `tempsPetits` | dans `remote_json` |
| Flag petits | `bouffePetits` | `"108"`, `bouffePetits` | `bouffePetits` | `bouffePetits` | dans `remote_json` |
| Flag gros | `bouffeGros` | `"109"`, `bouffeGros` | `bouffeGros` | `bouffeGros` | dans `remote_json` |

Les noms d’horaires (matin, midi, soir) sont harmonisés : l’interface de contrôle serveur (page contrôle, paramètres) et le contrat POST/GET utilisent les mêmes clés `bouffeMatin`, `bouffeMidi`, `bouffeSoir` (plus de variante courte type bouffeMat/bouffeMid).

---

## 4. Variables Configuration Seuils

| Description | Serveur POST | Serveur GET | Firmware C++ | API Locale `/dbvars` | NVS |
|-------------|--------------|-------------|--------------|---------------------|-----|
| Seuil aquarium | `aqThreshold` | `"102"`, `aqThr` | `aqThresholdCm` | `aqThreshold` | dans `remote_json` |
| Seuil reservoir | `tankThreshold` | `"103"`, `taThr` | `tankThresholdCm` | `tankThreshold` | dans `remote_json` |
| Seuil chauffage | `chauffageThreshold` (float) | `"104"`, `chauff` | `heaterThresholdC` | `chauffageThreshold` | dans `remote_json` |
| Limite flood | `limFlood` | `"114"`, `limFlood` | `limFlood` | `limFlood` | dans `remote_json` |
| Duree remplissage | `tempsRemplissageSec` | `"113"`, `tempsRemplissageSec` | `refillDurationMs` (ms interne) / `refillDurationSec` (s pour sync) | `tempsRemplissageSec` | dans `remote_json` |

**Note:** Cote firmware, `automatism.h` utilise `refillDurationMs` (millisecondes). Le serveur et l'API utilisent `tempsRemplissageSec` (secondes). GPIOMap internalName: `refillDurationSec` pour la valeur en secondes envoyee au serveur.

### Fallbacks existants

- `chauffageThreshold` / `heaterThreshold` (API accepte les deux)
- `tempsRemplissageSec` / `refillDuration` (API accepte les deux)

---

## 5. Variables Systeme

| Description | Serveur POST | Serveur GET | Firmware C++ | API Locale | NVS (sys) |
|-------------|--------------|-------------|--------------|------------|-----------|
| Déclenchement vérif. OTA | - | `triggerOtaCheck` (bool) | - | - | - |
| Force wakeup | `WakeUp` | `"115"`, `WakeUp` | `forceWakeUp` | `forceWakeUp` (clé JSON API locale) | `force_wake_up` |
| Frequence wakeup | `FreqWakeUp` | `"116"`, `FreqWakeUp` | `freqWakeSec` | `FreqWakeUp` | - |
| Email | `mail` | `"100"`, `mail` | `_emailAddress` | `mail` | - |
| Notif email | `mailNotif` | `"101"`, `mailNotif` | - | `mailNotif` | - |
| Reset mode | `resetMode` | `"110"`, `resetMode` | - | - | - |

---

## 6. Variables WiFi (API Locale uniquement)

### Endpoints `/json` et `/wifi/status` (convention harmonisee)

Les deux endpoints utilisent maintenant la meme convention avec prefixes `wifiSta*` / `wifiAp*` :

| Description | Cle JSON |
|-------------|----------|
| STA connecte | `wifiStaConnected` |
| STA SSID | `wifiStaSSID` |
| STA IP | `wifiStaIP` |
| STA RSSI | `wifiStaRSSI` |
| STA MAC | `wifiStaMac` |
| AP actif | `wifiApActive` |
| AP SSID | `wifiApSSID` |
| AP IP | `wifiApIP` |
| AP clients | `wifiApClients` |
| Mode WiFi | `wifiMode` |

**Status** : Harmonisation effectuee le 2026-01-31

---

## 7. Cles NVS

### Namespace SYSTEM (`sys`)

| Cle | Type | Description | Status |
|-----|------|-------------|--------|
| `force_wake_up` | bool | Force le mode eveille | **NOUVELLE** (migration depuis `forceWakeUp`) |
| `ota_upd_flag` | bool | Flag mise a jour OTA activee (cle stockee, limite 15 car. ESP-IDF) | OK |
| `ota_prevVer` | string | Version firmware precedente avant OTA | OK |
| `ota_in_prog` | bool | OTA en cours (cle stockee, limite 15 car.) | OK |
| `net_send_en` | bool | Envoi distant active | OK |
| `net_recv_en` | bool | Reception distante activee | OK |
| `rtc_epoch` | uint32 | Epoch RTC | OK |

### Namespace CONFIG (`cfg`)

| Cle | Type | Description | Status |
|-----|------|-------------|--------|
| `bouffe_matin` | bool | Flag deja nourri ce matin (valeur horaire = gpio_feedMorn, cf. cles dynamiques) | OK |
| `bouffe_midi` | bool | Flag deja nourri ce midi | OK |
| `bouffe_soir` | bool | Flag deja nourri ce soir | OK |
| `bouffe_jour` | int | Jour du dernier nourrissage (1-31) | OK |
| `bf_pmp_lock` | bool | Verrouillage pompe nourrissage | OK |
| `remote_json` | string | Config distante JSON | OK |
| `email` | string | Adresse email (cle fixe, pas gpio_*) | OK |
| `temp_last_ok` | float | Derniere temperature eau valide (fallback capteur) | OK |

#### Stockage NVS : remote_json vs cles gpio_*

En NVS (namespace `cfg`), la **configuration** (heures, seuils, durees, email, etc.) est stockee dans une seule cle : **`remote_json`** (JSON). Le firmware n'ecrit pas de cles `gpio_feedMorn`, `gpio_aqThr`, etc. en NVS pour ces parametres (voir `gpio_parser.cpp` : seuls les type ACTUATOR sont persistes en cles `gpio_*`). Les noms `gpio_*` ci-dessous servent au mapping GET/POST serveur et a l'API locale `/dbvars` ; la source de verite persistee pour la config est `remote_json`.

#### Cles GPIO dynamiques (namespace `cfg`)

Les cles suivantes sont utilisees pour le contrat serveur et l'API ; en NVS, seules les **actionneurs** (bool) sont ecrites sous forme de cles `gpio_*` individuelles (pump_aqua, pump_tank, heater, light, etc.). Les parametres (heures, seuils, durees) sont lus/ecrits via **remote_json** :

| Cle | Type | Description | GPIO |
|-----|------|-------------|------|
| `gpio_pump_aqua` | bool | Pompe aquarium | 16 |
| `gpio_pump_tank` | bool | Pompe reservoir | 18 |
| `gpio_heater` | bool | Chauffage | 2 |
| `gpio_light` | bool | Lumiere | 15 |
| `gpio_feed_small` | bool | Nourrir petits | 108 |
| `gpio_feed_big` | bool | Nourrir gros | 109 |
| `gpio_emailEn` | bool | Notifications email | 101 |
| `gpio_aqThr` | int | Seuil aquarium (cm) | 102 |
| `gpio_tankThr` | int | Seuil reservoir (cm) | 103 |
| `gpio_heatThr` | float | Seuil chauffage (C) | 104 |
| `gpio_feedMorn` | int | Heure matin | 105 |
| `gpio_feedNoon` | int | Heure midi | 106 |
| `gpio_feedEve` | int | Heure soir | 107 |
| `gpio_reset` | bool | Reset ESP32 | 110 |
| `gpio_feedBigD` | int | Duree gros (s) | 111 |
| `gpio_feedSmD` | int | Duree petits (s) | 112 |
| `gpio_refillD` | int | Duree remplissage (s) | 113 |
| `gpio_limFlood` | int | Limite inondation (cm) | 114 |
| `gpio_wakeup` | bool | Forcer reveil | 115 |
| `gpio_freqWake` | int | Frequence reveil (s) | 116 |

### Namespace STATE (`state`)

| Cle | Type | Description | Status |
|-----|------|-------------|--------|
| `snap_pending` | bool | Snapshot actionneurs en attente | OK |
| `snap_aqua` | bool | Etat pompe aquarium (snapshot) | OK |
| `snap_heater` | bool | Etat chauffage (snapshot) | OK |
| `snap_light` | bool | Etat lumiere (snapshot) | OK |
| `state_lastLocal` | uint32 | Timestamp derniere action locale | OK |
| `sync_count` | int | Nombre elements en attente de sync | OK |
| `sync_lastSync` | uint32 | Timestamp derniere synchronisation | OK |
| `sync_config` | bool | Config en attente de sync | OK |

#### Cles dynamiques STATE (generees a l'execution)

| Format | Type | Description |
|--------|------|-------------|
| `state_{actuator}` | bool | Etat actuel d'un actionneur (ex: `state_pumpAqua`, `state_heater`) |
| `sync_{actuator}` | bool | Flag de sync pour un actionneur (ex: `sync_pumpAqua`) |
| `sync_item_{index}` | string | Element en attente de sync (index 0-4, max 5) |

**Actionneurs valides :** `pumpAqua`, `pumpTank`, `heater`, `light`

### Namespace LOGS (`logs`)

| Cle | Type | Description | Status |
|-----|------|-------------|--------|
| `diag_rebootCnt` | int | Compteur de redemarrages | OK |
| `diag_minHeap` | uint32 | Heap minimum historique | OK |
| `diag_httpOk` | uint32 | Compteur HTTP reussies | OK |
| `diag_httpKo` | uint32 | Compteur HTTP echecs | OK |
| `diag_otaOk` | uint32 | Compteur OTA reussies | OK |
| `diag_otaKo` | uint32 | Compteur OTA echecs | OK |
| `diag_lastUptime` | uint32 | Dernier uptime (debug) | OK |
| `diag_lastHeap` | uint32 | Dernier heap (debug) | OK |
| `diag_crashFlag` | bool | Flag panic detecte (ex-diag_hasPanic, v11.173) | OK |
| `diag_panicCause` | string | Cause du panic | OK |
| `alert_flood_ts` | uint32 | Timestamp dernier email inondation | OK |
| `crash_has` | bool | Flag crash detecte | OK |
| `crash_reason` | int | Raison du crash | OK |

### Migration effectuee (2026-01-31)

Toutes les cles NVS utilisent maintenant la convention snake_case. Cles actuelles (source : `include/nvs_keys.h`) :
- `force_wake_up`, `rtc_epoch`
- `ota_upd_flag` (limite 15 car. ESP-IDF), `ota_prevVer`, `ota_in_prog`
- `temp_last_ok` (temperature eau fallback)
- `alert_flood_ts` (timestamp alerte inondation)

---

## 8. Clés Legacy GPIO (Serveur Distant)

Ces cles numeriques sont utilisees en BDD et doivent etre conservees indefiniment :

| GPIO | Description | Cle canonique |
|------|-------------|---------------|
| `"2"` | Chauffage | `heat` |
| `"15"` | Lumiere UV | `light` |
| `"16"` | Pompe aquarium | `pump_aqua` |
| `"18"` | Pompe reservoir | `pump_tank` |
| `"100"` | Email | `mail` |
| `"101"` | Notif email | `mailNotif` |
| `"102"` | Seuil aquarium | `aqThreshold` |
| `"103"` | Seuil reservoir | `tankThreshold` |
| `"104"` | Seuil chauffage | `chauffageThreshold` |
| `"105"` | Heure matin | `bouffeMatin` |
| `"106"` | Heure midi | `bouffeMidi` |
| `"107"` | Heure soir | `bouffeSoir` |
| `"108"` | Flag petits | `bouffePetits` |
| `"109"` | Flag gros | `bouffeGros` |
| `"110"` | Reset mode | `resetMode` |
| `"111"` | Duree gros | `tempsGros` |
| `"112"` | Duree petits | `tempsPetits` |
| `"113"` | Duree remplissage | `tempsRemplissageSec` |
| `"114"` | Limite flood | `limFlood` |
| `"115"` | Wake up | `WakeUp` |
| `"116"` | Frequence wakeup | `FreqWakeUp` |

---

## 9. Résumé des Actions d'Harmonisation

### Corrections effectuees (2026-02-15, audit gestion du temps)

1. **Documentation temps** : Nouvelle section §0 « Gestion du temps » (timezone firmware UTC+1, RTC/NVS, horaires, timestamps état ; stockage serveur APP_TIMEZONE ; alignement firmware/serveur). Référence croisée avec `ffp3/docs/TIMEZONE_MANAGEMENT.md` et `ffp3/docs/ENDPOINTS_ESP32_SERVEUR.md` (timestamp/signature, SIG_VALID_WINDOW).
2. **Serveur distant** : `chauffageThreshold` typé `?float` dans SensorData.php (aligné firmware, évite perte de précision)
3. **API locale** : `forceWakeup` harmonisé vers `forceWakeUp` (web_routes_status, realtime_websocket, common.js)
4. **web_client.cpp** : utilisation de SensorMap et GPIOMap pour noms POST (source unique gpio_mapping.h)
5. **Doc (v1.5)** : section « Sources de vérité » et procédure de synchronisation (audit redondance firmware/serveur) ; correction NVS §3 (heures = gpio_feedMorn / gpio_feedNoon / gpio_feedEve, pas bouffe_matin/midi/soir qui sont les flags « déjà nourri »)

### Corrections effectuees (2026-01-31)

1. **NVS (Phase 2)** : Migration avec fallback implementee
   - `forceWakeUp` -> `force_wake_up` (avec fallback lecture ancienne cle)
   - `ota_inProgress` -> `ota_in_progress` (avec suppression ancienne cle)
   - `temp_lastValid` -> `temp_last_valid` (avec fallback lecture ancienne cle)

2. **Endpoint `/wifi/status` (Phase 3)** : Prefixes harmonises
   - `staConnected` -> `wifiStaConnected`
   - `staSSID` -> `wifiStaSSID`
   - `staIP` -> `wifiStaIP`
   - `staRSSI` -> `wifiStaRSSI`
   - `staMac` -> `wifiStaMac`
   - `apActive` -> `wifiApActive`
   - `apSSID` -> `wifiApSSID`
   - `apIP` -> `wifiApIP`
   - `apClients` -> `wifiApClients`
   - `mode` -> `wifiMode`

3. **Struct `Measurements` (Phase 4)** : Renommage effectue
   - `humid` -> `humidity`

### Ne pas modifier (serveur distant = reference)

- Cles POST : `TempAir`, `EauAquarium`, `etatPompeAqua`, etc.
- Cles GET : `"105"`, `aqThr`, `pump_aqua`, etc.
- GPIO numeriques : `"2"`, `"15"`, `"16"`, etc.

### Corrections effectuees (v11.171 - Harmonisation types API)

4. **Endpoint `/dbvars` (Phase 5)** : Types de donnees harmonises
   - `mailNotif` : retourne maintenant `bool` (true/false) au lieu de string ("checked"/"")
   - `bouffePetits` : retourne maintenant `int` (0/1) au lieu de string ("0"/"1")
   - `bouffeGros` : retourne maintenant `int` (0/1) au lieu de string ("0"/"1")

### A faire plus tard (Phase 6 - apres plusieurs mois de stabilite)

- Supprimer les fallbacks NVS pour anciennes cles
- Supprimer les alias JSON legacy (`heaterThreshold`, `refillDuration`, cles numeriques "105"/"106" dans le parsing config)
- Cote firmware : deriver les cles textuelles du parsing (gpio_parser, config, automatism) depuis GPIOMap pour eviter listes paralleles

### Contrat API (échanges firmware / serveur)

- **API_KEY** : cote firmware dans `include/config.h` (ApiConfig::API_KEY) ; cote serveur dans `.env` (API_KEY). Ne pas dupliquer la valeur en clair dans la doc ; une seule reference (deploiement ou commentaire) suffit.
- **GET outputs/state** : la reponse peut contenir `dataStates`, `dataStatesReadingTime` (pour la page de controle) ; l'ESP32 **ignore** ces champs et n'utilise que les cles GPIO (numeriques ou symboliques) et `triggerOtaCheck`. Voir aussi `ffp3/docs/ENDPOINTS_ESP32_SERVEUR.md`.

---

## 10. Conventions de Style

| Couche | Convention | Exemples |
|--------|------------|----------|
| **Serveur POST** | PascalCase/camelCase mixte (impose) | `TempAir`, `etatPompeAqua` |
| **Serveur GET** | GPIO numeriques + cles abregees (impose) | `"105"`, `aqThr` |
| **Firmware C++** | camelCase variables, _camelCase membres prives | `tempWater`, `_lastReadings` |
| **API JSON locale** | camelCase coherent | `tempWater`, `wifiStaConnected` |
| **NVS** | snake_case (cible) | `force_wake_up`, `ota_update_flag` |
| **Constantes C++** | UPPER_SNAKE_CASE | `MAX_PUMP_RETRIES` |

---

*Derniere mise a jour : 2026-02-15 - v1.6 Section §0 Gestion du temps (timezone firmware/serveur, RTC, horaires, timestamps), audit gestion du temps*
