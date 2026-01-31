# Convention de Nommage des Variables - FFP5CS

**Version:** 1.2  
**Date:** 2026-01-31  
**Statut:** Documentation de reference

**Source de verite code:** `include/gpio_mapping.h` (VariableRegistry v11.172)

---

## Vue d'ensemble

Ce document centralise les conventions de nommage entre les differentes couches du systeme FFP5CS :
- **Serveur distant** (reference absolue - iot.olution.info)
- **Firmware ESP32** (variables C++ internes)
- **NVS** (stockage persistant local)
- **API JSON locale** (endpoints HTTP locaux)

---

## 1. Variables Capteurs

| Description | Serveur POST | Serveur GET | Firmware C++ | API Locale `/json` | NVS |
|-------------|--------------|-------------|--------------|-------------------|-----|
| Temperature air | `TempAir` | - | `tempAir` | `tempAir` | - |
| Humidite | `Humidite` | - | `humidity` (SensorReadings) / `humid` (Measurements) | `humidity` | - |
| Temperature eau | `TempEau` | - | `tempWater` | `tempWater` | `temp_lastValid` (cfg) |
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
| Heure matin | `bouffeMatin` | `"105"`, `bouffeMat` | `bouffeMatin` | `bouffeMatin` | `bouffe_matin` |
| Heure midi | `bouffeMidi` | `"106"`, `bouffeMid` | `bouffeMidi` | `bouffeMidi` | `bouffe_midi` |
| Heure soir | `bouffeSoir` | `"107"`, `bouffeSoir` | `bouffeSoir` | `bouffeSoir` | `bouffe_soir` |
| Duree gros | `tempsGros` | `"111"`, `tempsGros` | `tempsGros` | `tempsGros` | - |
| Duree petits | `tempsPetits` | `"112"`, `tempsPetits` | `tempsPetits` | `tempsPetits` | - |
| Flag petits | `bouffePetits` | `"108"`, `bouffePetits` | `bouffePetits` | `bouffePetits` | - |
| Flag gros | `bouffeGros` | `"109"`, `bouffeGros` | `bouffeGros` | `bouffeGros` | - |

---

## 4. Variables Configuration Seuils

| Description | Serveur POST | Serveur GET | Firmware C++ | API Locale `/dbvars` | NVS |
|-------------|--------------|-------------|--------------|---------------------|-----|
| Seuil aquarium | `aqThreshold` | `"102"`, `aqThr` | `aqThresholdCm` | `aqThreshold` | - |
| Seuil reservoir | `tankThreshold` | `"103"`, `taThr` | `tankThresholdCm` | `tankThreshold` | - |
| Seuil chauffage | `chauffageThreshold` | `"104"`, `chauff` | `heaterThresholdC` | `chauffageThreshold` | - |
| Limite flood | `limFlood` | `"114"`, `limFlood` | `limFlood` | `limFlood` | - |
| Duree remplissage | `tempsRemplissageSec` | `"113"`, `tempsRemplissageSec` | `refillDurationMs` | `tempsRemplissageSec` | - |

### Fallbacks existants

- `chauffageThreshold` / `heaterThreshold` (API accepte les deux)
- `tempsRemplissageSec` / `refillDuration` (API accepte les deux)

---

## 5. Variables Systeme

| Description | Serveur POST | Serveur GET | Firmware C++ | API Locale | NVS (sys) |
|-------------|--------------|-------------|--------------|------------|-----------|
| Force wakeup | `WakeUp` | `"115"`, `WakeUp` | `forceWakeUp` | `forceWakeUp` | `forceWakeUp` |
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
| `ota_update_flag` | bool | Flag mise a jour OTA activee | OK |
| `ota_in_progress` | bool | OTA en cours | **NOUVELLE** (migration depuis `ota_inProgress`) |
| `net_send_en` | bool | Envoi distant active | OK |
| `net_recv_en` | bool | Reception distante activee | OK |
| `rtc_epoch` | uint32 | Epoch RTC | OK |

### Namespace CONFIG (`cfg`)

| Cle | Type | Description | Status |
|-----|------|-------------|--------|
| `bouffe_matin` | int | Heure nourrissage matin | OK |
| `bouffe_midi` | int | Heure nourrissage midi | OK |
| `bouffe_soir` | int | Heure nourrissage soir | OK |
| `bouffe_jour` | int | Jour de nourrissage | OK |
| `bf_pmp_lock` | bool | Verrouillage pompe | OK |
| `remote_json` | string | Config distante JSON | OK |
| `gpio_email` | string | Adresse email | OK |
| `temp_last_valid` | float | Derniere temperature valide | **NOUVELLE** (migration depuis `temp_lastValid`) |

#### Cles GPIO dynamiques (namespace `cfg`)

Ces cles sont generees dynamiquement avec le prefixe `gpio_` :

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
| `diag_hasPanic` | bool | Flag panic detecte | OK |
| `diag_panicCause` | string | Cause du panic | OK |
| `alert_floodLast` | uint32 | Timestamp dernier email inondation | OK |
| `crash_has` | bool | Flag crash detecte | OK |
| `crash_reason` | int | Raison du crash | OK |

### Migration effectuee (2026-01-31)

Toutes les cles NVS utilisent maintenant la convention snake_case :
- `forceWakeUp` -> `force_wake_up` (avec fallback)
- `ota_inProgress` -> `ota_in_progress` (avec suppression ancienne cle)
- `temp_lastValid` -> `temp_last_valid` (avec fallback)

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
- Supprimer les alias JSON legacy (`heaterThreshold`, etc.)

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

*Derniere mise a jour : 2026-01-31 - Harmonisation Phase 1-4 completee*
