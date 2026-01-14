# Verification coherence communication serveur ffp3 <-> ESP32

## Perimetre
- Serveur distant (ffp3): routes HTTP, validation et format des reponses.
- ESP32: endpoints utilises, payloads envoyes, parsing des reponses.

## Cartographie des endpoints (serveur -> ESP32)
- PROD:
  - POST `/ffp3/post-data` -> `PostDataController::handle`
  - GET `/ffp3/api/outputs/state` -> `OutputController::getOutputsState`
  - POST `/ffp3/heartbeat` -> `HeartbeatController::handle`
- TEST:
  - POST `/ffp3/post-data-test`
  - GET `/ffp3/api/outputs-test/state`
  - POST `/ffp3/heartbeat-test`

Sources:
- `ffp3/public/index.php`
- `include/config.h` (ESP32)

## Contrat POST capteurs/etats (ESP32 -> serveur)

### Champs envoyes par l'ESP32
Le firmware construit un payload `application/x-www-form-urlencoded` et ajoute `api_key` + `sensor` si absents.

Champs systematiquement envoyes:
- `api_key` (ajoute par `postRaw`)
- `sensor` (ajoute par `postRaw`, valeur `ProjectConfig::BOARD_TYPE`)
- `version`, `TempAir`, `Humidite`, `TempEau`, `EauPotager`, `EauAquarium`, `EauReserve`,
  `diffMaree`, `Luminosite`, `etatPompeAqua`, `etatPompeTank`, `etatHeat`, `etatUV`
- Champs systematiquement envoyes par `AutomatismSync::sendFullUpdate`:
  `bouffeMatin`, `bouffeMidi`, `bouffeSoir`, `bouffePetits`, `bouffeGros`,
  `tempsGros`, `tempsPetits`, `tempsRemplissageSec`, `limFlood`, `WakeUp`, `FreqWakeUp`,
  `mail`, `mailNotif`
- Seuils envoyes: `aqThreshold`, `tankThreshold`, `chauffageThreshold`

Source:
- `src/web_client.cpp`

### Champs attendus par le serveur
Le serveur parse les champs suivants et les accepte comme `null` si vides:
- `sensor`, `version`, `TempAir`, `Humidite`, `TempEau`, `EauPotager`, `EauAquarium`, `EauReserve`,
  `diffMaree`, `Luminosite`, `etatPompeAqua`, `etatPompeTank`, `etatHeat`, `etatUV`,
  `bouffeMatin`, `bouffeMidi`, `bouffePetits`, `bouffeGros`, `aqThreshold`, `tankThreshold`,
  `chauffageThreshold`, `mail`, `mailNotif`, `resetMode`, `bouffeSoir`,
  `tempsGros`, `tempsPetits`, `tempsRemplissageSec`, `limFlood`, `WakeUp`, `FreqWakeUp`

Source:
- `ffp3/src/Controller/PostDataController.php`
- `ffp3/src/Domain/SensorData.php`

Insertion BDD:
- `SensorRepository::insert` insere les champs de base dans `ffp3Data/ffp3Data2`.
- Les parametres de config (GPIO 111-116, etc.) sont synchronises via `OutputRepository::syncStatesFromSensorData`.

Source:
- `ffp3/src/Repository/SensorRepository.php`
- `ffp3/src/Repository/OutputRepository.php`

## Contrat GET outputs (serveur -> ESP32)

### Format de reponse serveur
JSON objet: paires `{ "gpio": state }` avec clefs numeriques sous forme de string.
Exemple: `{ "16": 1, "100": "email@...", "102": "18", ... }`

Normalisation:
- GPIO boolens (0-99, 101, 108, 109, 110, 115) -> entiers 0/1.
- Autres GPIO -> valeurs brutes (string ou numerique).

Sources:
- `ffp3/src/Controller/OutputController.php`
- `ffp3/src/Service/OutputCacheService.php`

### Parsing cote ESP32
- Attendu: clefs numeriques "2","15","16","18","100"... (mapping central `GPIOMap`).
- Fallback: accepte aussi un wrapper `outputs` et des cles textuelles (`aqThreshold`, `mail`, etc.).
- Conversion robuste des types (bool, int, float, string).

Sources:
- `include/gpio_mapping.h`
- `src/gpio_parser.cpp`
- `src/automatism/automatism_network.cpp`

## Contrat Heartbeat (ESP32 -> serveur)
Payload `application/x-www-form-urlencoded`:
- `uptime`, `free`, `min`, `reboots`, `crc`
- `crc` = CRC32 de la chaine `uptime=...&free=...&min=...&reboots=...`

Sources:
- `src/web_client.cpp`
- `ffp3/src/Controller/HeartbeatController.php`

## Resultat: coherence globale

### OK
- Endpoints prod/test alignes (base `/ffp3` cote serveur, profils dans `config.h` cote ESP32).
- Payload POST compatible: noms de champs et types attendus concordants.
- JSON outputs coherent avec le parsing ESP32 (clefs numeriques, conversions robustes).
- Heartbeat coherent (CRC32 compatible, champs requis alignes).

### Points d'attention (risques)
1) **API_KEY**: le serveur exige `API_KEY` dans `.env`. Verifier que la valeur cote serveur correspond a `ApiConfig::API_KEY` dans l'ESP32.
2) **HTTPS**: l'ESP32 force HTTPS et accepte tous les certificats; verifier que le serveur expose bien HTTPS pour `iot.olution.info`.

## Recommandations minimales
- Verifier `API_KEY` cote serveur (`ffp3/.env`) et cote ESP32 (`include/config.h`).
- S'assurer que l'ESP32 utilise bien `AutomatismSync::sendFullUpdate` pour l'envoi complet.
- Continuer a garder le format JSON "gpio -> state" sans wrapper pour simplifier le parsing (le fallback existe deja).
